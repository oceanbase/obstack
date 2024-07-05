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

#ifndef LLVM_DWARFDUMP_H_
#define LLVM_DWARFDUMP_H_

#include <vector>
#include <string>

namespace _obstack
{
struct LineInfo
{
  std::string filename_="";
  unsigned int line_=0;
};

class LLVMDwarfDump
{
public:
  LLVMDwarfDump(const char *file);
  void addr2line(std::vector<ulong> &addrs, std::vector<_obstack::LineInfo> &line_infos);
private:
  std::vector<std::string> objs_;
};

}

#endif // LLVM_DWARFDUMP_H_
