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

#ifndef COMMON_UTIL_H_
#define COMMON_UTIL_H_

#include <iostream>
#include <utility>
#include <string>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "common/log.h"

namespace _obstack
{
namespace common
{
inline bool startwith(const char *fullstring, const char *starting)
{
  size_t full_len = strlen(fullstring);
  size_t starting_len = strlen(starting);
  if (full_len >= starting_len) {
    if (!strncmp(fullstring, starting, starting_len))
      return true;
  } else {
    return false;
  }
  return false;
}

inline bool endwith(std::string s, std::string sub)
{
  return s.rfind(sub) == (s.length()-sub.length());
}

inline bool file_exist(const std::string &name)
{
  struct stat buffer;
  return (stat (name.c_str(), &buffer) == 0);
}

inline int64_t current_time()
{
  int err_ret = 0;
  struct timeval t;
  if (__builtin_expect((err_ret = gettimeofday(&t, nullptr)) < 0, 0)) {
    LOG(ERROR, "gettimeofday error, err_ret: %d, errno: %d", err_ret, errno);
    abort();
  }
  return (static_cast<int64_t>(t.tv_sec) * 1000000L +
          static_cast<int64_t>(t.tv_usec));
}

inline char *ltrim(char *s)
{
  while(isspace(*s)) s++;
  return s;
}

inline char *rtrim(char *s)
{
  char* back = s + strlen(s);
  while(isspace(*--back));
  *(back+1) = '\0';
  return s;
}

inline char *trim(char *s)
{
  return rtrim(ltrim(s));
}

inline void inplace_reverse(char * str)
{
  if (str) {
    char * end = str + strlen(str) - 1;

#   define XOR_SWAP(a,b) do                     \
      {                                         \
        a ^= b;                                 \
        b ^= a;                                 \
        a ^= b;                                 \
      } while (0)

      while (str < end)
      {
        XOR_SWAP(*str, *end);
        str++;
        end--;
      }
#   undef XOR_SWAP
  }
}

}
}

#endif  // COMMON_UTIL_H_
