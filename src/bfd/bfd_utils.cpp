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

#include "bfd_utils.h"

#include <algorithm>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <link.h>
#include <assert.h>
#include <sys/time.h>
#include <libelf.h>
#include <gelf.h>
#include "common/config.h"
#include "common/log.h"
#include "common/error.h"
#include "common/log.h"
#include "utils/util.h"
#include "utils/defer.h"

using namespace std;

namespace _obstack
{
namespace bfdutils
{
struct bfd_data
{
  const char *function;
};

bool check_shlib(const string& file, ulong &vaddr) {
  FILE *fp = fopen(file.c_str(), "r");
  if (fp == 0) {
    return false;
  }
  elf_version(EV_CURRENT);
  Elf* elf = elf_begin(fileno(fp), ELF_C_READ, nullptr);
  if (elf == 0) {
    return false;
  }
  vaddr = 0;
#if defined(__i386__)
  Elf32_Ehdr* const ehdr = elf32_getehdr(elf);
  Elf32_Phdr* const phdr = elf32_getphdr(elf);
#else
  Elf64_Ehdr* const ehdr = elf64_getehdr(elf);
  Elf64_Phdr* const phdr = elf64_getphdr(elf);
#endif
  if (!ehdr) {
    return false;
  }
  const int num_phdr = ehdr->e_phnum;
  for (int i = 0; i < num_phdr; ++i) {
#if defined(__i386__)
    Elf32_Phdr* const p = phdr + i;
#else
    Elf64_Phdr* const p = phdr + i;
#endif
    if (p->p_type == PT_LOAD && (p->p_flags & 1) != 0) {
      vaddr = p->p_vaddr;
      break;
    }
  }
  elf_end(elf);
  fclose(fp);
  return vaddr == 0;
}

bool check_stripped(const char *file)
{
  bool stripped = true;
  int fd;

  Elf *elf;
  Elf_Scn *scn;
  GElf_Shdr shdr;

  elf_version(EV_CURRENT);
  if((fd = open(file, O_RDONLY)) < 0)
  {
    return false;
  }
  DEFER(close(fd));

  elf = elf_begin(fd, ELF_C_READ, NULL);
  DEFER(elf_end(elf));

  scn = NULL;

  while((scn = elf_nextscn(elf, scn)) != NULL)
  {
    gelf_getshdr(scn, &shdr);

    if(shdr.sh_type == SHT_SYMTAB)
    {
      stripped = false;
      break;
    }
  }
  return stripped;
}

bool in_range(ulong addr, PTLoad *pt_load)
{
  return (addr >= pt_load->addr_start_) &&
    (addr < pt_load->addr_end_);
}

bfd *open_bfd(const char *file)
{
  bfd *abfd;
  char **matching;

  if (!common::file_exist(string(file))) {
    LOG(WARN, "file not exist: %s", file);
    return NULL;
  }
  abfd = bfd_openr(file, NULL);
  if (abfd == NULL) {
    LOG(WARN, "bfd_openr failed");
    return NULL;
  }

  if (bfd_check_format(abfd, bfd_archive)) {
    LOG(WARN, "bfd_check_format failed");
    bfd_close(abfd);
    return NULL;
  }

  if (!bfd_check_format_matches(abfd, bfd_object, &matching)) {
    LOG(WARN, "bfd_check_format_matches failed");
    free(matching);
    bfd_close(abfd);
    return NULL;
  }

  return abfd;
}

SymbolTable *BFDInfo::load_symbols(SymbolTable *st)
{
  if (check_stripped(abfd_->filename)) {
    LOG(DEBUG, "no symbols, file: %s", abfd_->filename);
    return nullptr;
  }
  asection* const text_sec = st->text_section_ = bfd_get_section_by_name(abfd_, ".text");
  if (text_sec) {
    st->text_vma = text_sec->vma;
    st->text_size = text_sec->size;
  }
  bool dynamic = false;
  uint size = 0;
  long symcnt = bfd_read_minisymbols(abfd_, 0, (void**)&syms_, &size);
  if (!symcnt) {
    if (syms_ != NULL) {
      free(syms_);
    }
    symcnt = bfd_read_minisymbols(abfd_, 1, (void**)&syms_, &size);
    dynamic = true;
  }

  asymbol* store = bfd_make_empty_symbol(abfd_);
  assert(store);

  bfd_byte* p = (bfd_byte*)syms_;
  bfd_byte* pend = p + symcnt * size;
  for (; p < pend; p += size) {
    asymbol* sym = bfd_minisymbol_to_symbol(abfd_, dynamic, p, store);
    if ((sym->flags & BSF_FUNCTION) == 0) {
      continue;
    }
    symbol_info sinfo;
    bfd_get_symbol_info(abfd_, sym, &sinfo);
    if (sinfo.type != 'T' && sinfo.type != 't' && sinfo.type != 'W' &&
        sinfo.type != 'w') {
      continue;
    }
    if (common::startwith(sinfo.name, "__tcf"))
      continue;
    if (common::startwith(sinfo.name, "__tz"))
      continue;
    st->sym_ents_.push_back({.addr_ = sinfo.value, .name_ = string(sinfo.name)});
  }
  std::sort(st->sym_ents_.begin(), st->sym_ents_.end(), [](SymbolEnt &l, SymbolEnt &r) {
                                                  return l.addr_ < r.addr_;
                                                });
  return st;
}

void trace_bfd_addr(BContext &bctx, PTLoad *pt_load , void *relative_addr, bfd_data *data)
{
  bfd *abfd = pt_load->st_->bfd_info_->abfd_;
  auto &sym_ents = pt_load->st_->sym_ents_;
  auto it = std::upper_bound(sym_ents.begin(), sym_ents.end(), relative_addr, [](void *addr, SymbolEnt &l) {
                                                                       return (ulong)addr < l.addr_;
                                                                   });
  if (it == sym_ents.end() || it == sym_ents.begin()) {
    LOG(WARN, "symbol not founded, pt_load: %p, relative_addr: %p", pt_load, relative_addr);
    return;
  } else {
    it--;
  }
  data->function = it->name_.c_str();
}

BFDCache::BFDCache()
{
}

void BFDCache::do_addr2symbol(BContext &bctx, void *addr)
{
  total++;
  auto it = loc_cache_.end();
  if ((it = loc_cache_.find(addr)) == loc_cache_.end()) {
    bfd_data data{.function="???"};
    auto pt_it = std::upper_bound(pt_loads_.begin(), pt_loads_.end(),
                                  (ulong)addr, [](ulong addr, PTLoad *l ) {
                                               return addr < l->addr_start_; });
    if (pt_it != pt_loads_.begin()) {
      pt_it--;
    } else {
      pt_it = pt_loads_.end();
    }
    if (pt_it != pt_loads_.end()) {
      PTLoad *pt_load = *pt_it;
      if (in_range((ulong)addr, pt_load)) {
        trace_bfd_addr(bctx, pt_load, (void*)addr2offset(pt_load, (ulong)addr), &data);
        it = loc_cache_.insert({addr, Location{.file_ = pt_load->st_->bfd_info_->file_.c_str(), .function_ = data.function,
                                               .filename_ = "???", .line_ = 0}}).first;
      }
    }
  } else {
    hit++;
  }
  auto &loc = it->second;
  if (it != loc_cache_.end()) {
    bctx.func_(bctx.arg_, loc.file_.c_str(), loc.function_.c_str(), loc.filename_.c_str(), loc.line_);
  } else {
    lack++;
    LOG(WARN, "no symbol founded: %p", addr);
    auto *empty = "???";
    bctx.func_(bctx.arg_, empty, empty, empty, 0);
  }
}

bfd *BFDInfo::init()
{
  abfd_ = open_bfd(file_.c_str());
  if (!abfd_) return nullptr;
  abfd_->flags |= BFD_DECOMPRESS;
  return abfd_;
}

PTLoad *BFDCache::create_new_pt_load(string &file, void *addr_start, void *addr_end, bool is_exe, bool load_symbols)
{
  SymbolTable *st = nullptr;
  ulong load_vaddr = 0;
  check_shlib(file, load_vaddr);
  auto it = st_map_.find(file);
  if (it != st_map_.end()) {
    st = it->second;
  } else {
    if (load_symbols) {
      BFDInfo *bfd_info = NULL;
      if (load_vaddr != 0) { // means executable file
        string symbol_file = CONF.symbol_path ?: file;
        string debuginfo_file = CONF.debuginfo_path ?: file;
        bfd_info =  new BFDInfo(symbol_file, debuginfo_file);
      } else {
        bfd_info =  new BFDInfo(file, file);
      }
      if (!bfd_info->init()) return nullptr;
      st = new SymbolTable();
      st->bfd_info_ = bfd_info;
      bfd_info->load_symbols(st);
    }
    st_map_.insert({file, st});
  }
  auto *pt_load = new PTLoad{.addr_start_ = (ulong)addr_start,
                             .addr_end_ = (ulong)addr_end,
                             .is_exe_ = is_exe,
                             .st_ = st,
                             .load_vaddr_ = load_vaddr};
  pt_loads_.push_back(pt_load);
  LOG(DEBUG, "create_new_pt_load, pt_load: %p, file: %s, addr_start: 0x%lx, load_vaddr: 0x%lx",
      pt_load, file.c_str(), addr_start, load_vaddr);
  return pt_load;
}

void BFDCache::sort_pt_load()
{
  std::sort(pt_loads_.begin(), pt_loads_.end(),
            [](const PTLoad *l, const PTLoad *r)
              { return l->addr_start_ < r->addr_start_; });
}

PTLoad *BFDCache::find_pt_load(ulong addr)
{
  PTLoad *pt_load = nullptr;
  auto pt_it = std::upper_bound(pt_loads_.begin(), pt_loads_.end(),
                                (ulong)addr, [](ulong addr, PTLoad *l) {
                                             return addr < l->addr_end_; });
  if (pt_it != pt_loads_.end()) {
    pt_load = *pt_it;
  }
  return pt_load;
}

}
}
