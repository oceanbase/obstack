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

#ifndef BFD_UTILS_H_
#define BFD_UTILS_H_

#include <unordered_map>
#include "config.h"
#include <vector>
#include <string>
#include <bfd.h>

namespace _obstack
{
namespace bfdutils
{
using std::string;
struct SymbolEnt
{
  ulong addr_;
  std::string name_;
};

struct BFDInfo;
struct SymbolTable
{
  asection *text_section_;
  ulong text_vma;
  ulong text_size;
  std::vector<SymbolEnt> sym_ents_;
  BFDInfo *bfd_info_;
};

struct BFDInfo
{
  string file_;
  string debug_file_;
  bfd *abfd_;
  asymbol **syms_;
  SymbolTable st_;
  BFDInfo(string &file, string &debug_file) : file_(file), debug_file_(debug_file) {}
  bfd *init();
  SymbolTable *load_symbols(SymbolTable *st);
};

struct PTLoad
{
  ulong addr_start_;
  ulong addr_end_;
  bool is_exe_;
  SymbolTable *st_;
  ulong load_vaddr_;
};

struct Location
{
  string file_;
  string function_;
  string filename_;
  unsigned int line_;
};

struct BContext
{
  typedef void (*Func) (void*, const char *file, const char *function,
                      const char *filename, int line);
  Func func_;
  void *arg_;
};

class BFDCache
{
public:
  BFDCache();
  PTLoad *create_new_pt_load(string &file, void *vaddr_start, void *vaddr_end, bool is_exe, bool load_symbols);
  void sort_pt_load();
  template<typename func>
  void addr2symbol(void *addr, func &&f)
  {
    BContext bctx;
    bctx.func_ = [](void *arg, const char *file, const char *function,
                    const char *filename, int line) {
                   (*(func*)arg)(file, function, filename, line);
                 };
    bctx.arg_ = &f;
    do_addr2symbol(bctx, addr);
  }
  PTLoad *find_pt_load(ulong addr);
  static ulong addr2offset(PTLoad *pt_load, ulong addr)
  {
    return (ulong)addr - (pt_load->addr_start_ - pt_load->load_vaddr_);
  }
private:
  void do_addr2symbol(BContext &bctx, void *addr);
private:
  std::unordered_map<string, SymbolTable*> st_map_;
  std::unordered_map<string, BFDInfo*> object_map_;
  std::unordered_map<void*, Location> loc_cache_;
  std::vector<PTLoad*> pt_loads_;
  int total = 0;
  int hit = 0;
  int lack = 0;
};
}
}

#endif // BFD_UTILS_H_
