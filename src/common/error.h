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

#ifdef DEF_ERR
DEF_ERR(NOT_INIT)
DEF_ERR(INVALID_ARG)
DEF_ERR(INIT_TWICE)
DEF_ERR(SIZE_OVERFLOW)
DEF_ERR(ENTRY_NOT_EXIST)
DEF_ERR(ALLOC_MEMORY_FAILED)
DEF_ERR(FILE_OPEN_ERROR)
DEF_ERR(FILE_NOT_EXIST)
DEF_ERR(EMPTY_FILE_ERROR)
DEF_ERR(CREATE_ENTRY_FAILED)
DEF_ERR(KILL_FAILED)
DEF_ERR(READLINK_FAILED)
DEF_ERR(OPEN_DIR_FAILED)
DEF_ERR(UNEXPECTED_ERROR)
DEF_ERR(TIMEOUT)
#endif

#ifndef COMMON_ERROR_H_
#define COMMON_ERROR_H_

#include <stdint.h>

namespace _obstack
{
namespace common
{

struct Error
{
  const char *enum_str_;
};

enum ErrorCodeEnum
{
  SUCCESS = 0,
  error_begin = 1000,
#define DEF_ERR(err_code) err_code,
#include <common/error.h>
#undef DEF_ERR
  error_end
};

class Errors
{
public:
  Errors()
  {
#define DEF_ERR(err_code) errs_[err_code] = {.enum_str_ = #err_code};
#include <common/error.h>
#undef DEF_ERR
  }
  Error errs_[error_end]={};
  static Errors &get_instance()
  {
    static Errors one;
    return one;
  }
};

template<typename ... Args>
inline void error(int err_code, const char *fmt="", Args && ... args) {
  if (err_code != 0) {
    auto &err = Errors::get_instance().errs_[err_code];
    char new_fmt[256];
    snprintf(new_fmt, sizeof(new_fmt), "err occurs!!! %%s(%%d), %s\n", fmt);
    fprintf(stderr, new_fmt, err.enum_str_, err_code, args...);
    exit(-1);
  }
}

}
}

#endif  // COMMON_ERROR_H_
