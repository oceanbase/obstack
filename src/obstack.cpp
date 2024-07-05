/**
 * Copyright (C) 2024 OceanBase

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "obstack.h"
#include<iostream>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <sys/wait.h>
#include <fcntl.h>
#define HAVE_DECL_BASENAME 1
#include <libiberty/demangle.h>
#undef HAVE_DECL_BASENAME
#include "bfd/bfd_utils.h"
#include "utils/defer.h"
#include "common/log.h"
#include "common/config.h"
#include "common/error.h"
#include "utils/util.h"
#include "utils/defer.h"
#include "utils/color_printf.h"
#include "llvmtool/llvm-dwarfdump.h"
using namespace std;

using namespace _obstack::common;
namespace _obstack
{
using namespace bfdutils;
ulong terminator = (ulong)-1;

char *get_demangled_symbol(const char* symbol_name) {
  uint arg = DMGL_ANSI;
  arg |= DMGL_PARAMS;
  arg |= DMGL_TYPES;
  char* demangled = cplus_demangle(symbol_name, arg);
  if (!demangled) {
    demangled = strdup(symbol_name);
  }
  return demangled;
}

bool is_same_file(const char *path1, const char *path2) {
  struct stat sb1, sb2;
  return 0 == stat(path1, &sb1)
    && 0 == stat(path2, &sb2)
    && sb1.st_dev == sb2.st_dev
    && sb1.st_ino == sb2.st_ino;
}

ObStack::ObStack(int pid)
  : pid_(pid) {}

void ObStack::read_maps(int pid)
{
  char exe[512];
  snprintf(exe, sizeof(exe), "/proc/%d/exe", pid);

  char fn[64];
  snprintf(fn, sizeof(fn), "/proc/%d/maps", pid);
  FILE *map_file = fopen(fn, "rt");
  if (!map_file) return;
  DEFER(fclose(map_file));
  char line[1024];
  int64_t inode = -1;
  char path[256];
  bool has_perm_e;
  int64_t min_addr;
  int64_t max_addr;
  char *r = NULL;
  do {
    int64_t next_start, next_end, next_inode, next_offset, next_major, next_minor;
    char next_perms[8];
    char next_path[256];
    bool yield = false;
    if (NULL == (r = fgets(line, sizeof(line), map_file))) {
      yield = true;
    } else {
      int n = sscanf(line,
                     "%lx-%lx %4s %lx %lx:%lx %ld %255s",
                     &next_start, &next_end, next_perms,
                     &next_offset, &next_major, &next_minor, &next_inode, next_path);
      if (n < 8) {
        // do-nothing
      } else if (next_inode != inode) {
        yield = true;
      } else {
        has_perm_e ?: strlen(next_perms) == 4 && 'x' == next_perms[2];
        if (next_start < min_addr) {
          min_addr = next_start;
        }
        if (next_end > max_addr) {
          max_addr = next_end;
        }
      }
    }
    if (yield) {
      if (inode > 0 && has_perm_e && path[0] != '[') {
        maps_.push_back(Map{.path_ = path,
              .start_ = (ulong)min_addr,
              .end_ = (ulong)max_addr,
              .is_exe_ = is_same_file(path, exe)});
      }
      inode = next_inode;
      memcpy(path, next_path, sizeof(path));
      has_perm_e = strlen(next_perms) == 4 && 'x' == next_perms[2];
      min_addr = next_start;
      max_addr = next_end;
    }
  } while (r);
}

void ObStack::load_maps(bfdutils::BFDCache &bfd_cache)
{
  int i = 0;
  string obs_path;
  for (auto &&map : maps_) {
    auto pt_load = bfd_cache.create_new_pt_load(map.path_, (void*)map.start_, (void*)map.end_, map.is_exe_, !CONF.no_parse);
    if (!pt_load) {
      LOG(WARN, "create pt load failed, file: %s", map.path_.c_str());
    }
  }
  bfd_cache.sort_pt_load();
}

void ObStack::add_bt(int tid, char *tname, std::vector<ulong> &&addrs, string &&bt)
{
  bts_.push_back({.tid_ = tid, .tname_ = string(tname), .addrs_ = addrs, .bt_ = bt});
}

template<typename Addrs>
void ObStack::print_stack_frames(Addrs &addrs, bool with_frame_no)
{
#define PREFIX "0x%016lx in"
  int frame = 0;
  for (auto &&addr : addrs) {
    auto it = loc_cache_.end();
    if (with_frame_no) {
      c_printf(COLOR_YELLOW, "#%-4d ", frame++);
    }
    if ((it = loc_cache_.find(addr)) != loc_cache_.end()) {
      auto &loc = *it->second;
      bool fn_valid =
        loc.filename_.length() > 0 &&
        0 != loc.filename_.compare("0") &&
        0 != loc.filename_.compare("(null)") &&
        loc.line_ > 0;
      if (fn_valid) {
        c_printf(COLOR_CYAN, PREFIX " %s at %s:%d\n", addr, loc.function_.c_str(),
               loc.filename_.c_str(), loc.line_);
      } else {
        c_printf(COLOR_CYAN, PREFIX " %s from %s\n", addr, loc.function_.c_str(),
               loc.file_.c_str());
      }
    } else {
      c_printf(COLOR_CYAN, PREFIX " ???\n", addr);
    }
  }
#undef PREFIX
}

void ObStack::gen_result()
{
  if (CONF.agg) {
    struct Value {
      vector<int> tids_;
      vector<string> tnames_;
      vector<ulong> *addrs_;
    };
    std::unordered_map<string, Value*> bt_map;
    for (auto &&bt : bts_) {
      auto it = bt_map.find(bt.bt_);
      if (it == bt_map.end()) {
        auto *val = new Value();
        val->addrs_ = &bt.addrs_;
        it = bt_map.insert({bt.bt_, val}).first;
      }
      it->second->tids_.push_back(bt.tid_);
      it->second->tnames_.push_back(bt.tname_);
    }
    using Iterator = decltype(bt_map)::iterator;
    std::vector<Iterator> sorted_bts;
    for (auto it = bt_map.begin(); it != bt_map.end(); it++) {
      sorted_bts.push_back(it);
    }
    std::sort(sorted_bts.begin(), sorted_bts.end(), [](Iterator &l, Iterator &r) {
                                                      return l->second->tids_.size() > r->second->tids_.size();
                                                    });
    for (auto &&it : sorted_bts) {
      c_printf(COLOR_YELLOW, "Threads (");
      auto &tids = it->second->tids_;
      auto &tnames = it->second->tnames_;
      for (int i = 0; i < tids.size(); i++) {
        c_printf(COLOR_YELLOW, "%s%d-%s", 0 == i ? "" : ", ", tids[i], tnames[i].c_str());
      }
      c_printf(COLOR_YELLOW, ")\n");
      print_stack_frames(*it->second->addrs_);
    }
  } else {
    for (auto &&bt : bts_) {
      c_printf(COLOR_YELLOW, "Thread %d (%s)\n", bt.tid_, bt.tname_.c_str());
      print_stack_frames(bt.addrs_);
    }
  }
}

int ObStack::stack_it()
{
  bfdutils::BFDCache bfd_cache;
  read_maps(pid_);
  load_maps(bfd_cache);
  if (CONF.no_parse) {
    for (auto &&bt : bts_) {
      c_printf(COLOR_CYAN, "tid: %d, tname: %s, bt:", bt.tid_, bt.tname_.c_str());
      for (auto addr : bt.addrs_) {
        ulong v = addr;
        PTLoad *pt_load = bfd_cache.find_pt_load(addr);
        if (pt_load && pt_load->is_exe_) {
          v = BFDCache::addr2offset(pt_load, addr);
        }
        c_printf(COLOR_CYAN, " 0x%lx", v);
      }
      c_printf(COLOR_CYAN, "\n");
    }
    return 0;
  }
  std::unordered_map<ulong, LineInfo*> addr_map;
  std::for_each(bts_.begin(), bts_.end(), [&](decltype(bts_[0]) &bt) {
                                            for (auto &&addr : bt.addrs_) {
                                              addr_map.insert({addr, new LineInfo()});
                                            }
                                          });
  LOG(DEBUG, "aggregated addrs count: %d", addr_map.size());
  std::unordered_map<std::string, std::vector<std::pair<ulong/*abs_address*/, ulong/*relative_address*/>> > file_addrs_map;
  for (auto && kv: addr_map) {
    auto addr = kv.first;
    auto *pt_load = bfd_cache.find_pt_load(addr);
    if (!pt_load) {
      LOG(WARN, "no pt load founded, addr: %p", addr);
    } else {
      string &file = pt_load->st_->bfd_info_->debug_file_;
      auto it = file_addrs_map.find(file);
      if (it == file_addrs_map.end()) {
        it = file_addrs_map.insert({file, decltype(it->second)()}).first;
      }
      auto offset = BFDCache::addr2offset(pt_load, addr);
      it->second.push_back(std::make_pair(addr, offset));
    }
  }
  for (auto &&kv : file_addrs_map) {
    auto &file = kv.first;
    auto &addr_pairs = kv.second;
    std::vector<ulong> addrs(addr_pairs.size());
    std::transform(addr_pairs.begin(), addr_pairs.end(), addrs.begin(), [](decltype(addr_pairs[0]) &addr_pair) { return addr_pair.second;});
    if (!common::file_exist(string(file))) {
      LOG(ERROR, "file not exist: %s", file.c_str());
      common::error(common::FILE_NOT_EXIST);
    }
    LLVMDwarfDump llvmdwdump(file.c_str());
    std::vector<LineInfo> line_infos(addrs.size());
    std::fill(line_infos.begin(), line_infos.end(), LineInfo());
    if (!CONF.no_lineno) {
      llvmdwdump.addr2line(addrs, line_infos);
    }
    for (int i = 0; i < addrs.size(); i++) {
      auto addr = addr_pairs[i].first;
      auto line_info = line_infos[i];
      bfd_cache.addr2symbol((void*)addr, [&](const char *file, const char *function,
                                             const char *filename, unsigned int line) {
                                           auto *func = get_demangled_symbol(function);
                                           loc_cache_.insert({addr, new Location{.file_ = file, .function_ = func,
                                                                                 .filename_ = line_info.filename_, .line_ = line_info.line_}});
                                         });
    }
  }
  gen_result();
  return 0;
}
}
