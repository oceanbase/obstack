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

#ifndef OBSTACK_H_
#define OBSTACK_H_

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace _obstack
{
namespace bfdutils
{
class BFDCache;
class Location;
}

class LineInfo;
class ObStack
{
 struct Map
 {
   std::string path_;
   ulong start_;
   ulong end_;
   bool is_exe_;
 };
 struct Bt
 {
   int tid_;
   std::string tname_;
   std::vector<ulong> addrs_;
   std::string bt_;
 };
public:
  ObStack(int pid);
  int stack_it();
  void add_bt(int tid, char *tname, std::vector<ulong> &&addrs, std::string &&bt);
private:
  void read_maps(int pid);
  void load_maps(bfdutils::BFDCache &bfd_cache);
  void gen_result();
  template<typename Addrs>
  void print_stack_frames(Addrs &addrs, bool witnh_frame_no=true);
private:
  int pid_;
  std::vector<Map> maps_;
  std::vector<Bt> bts_;
  std::unordered_map<ulong, bfdutils::Location*> loc_cache_;
};
}

#endif // OBSTACK_H_
