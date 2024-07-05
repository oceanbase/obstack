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

#ifdef DEF_CONF
DEF_CONF(int, pid, -1)
DEF_CONF(bool, no_parse, false)
DEF_CONF(bool, agg, false)
DEF_CONF(const char*, symbol_path, nullptr)
DEF_CONF(const char*, debuginfo_path, nullptr)
DEF_CONF(bool, no_lineno, false)
DEF_CONF(bool, thread_only, false)
#endif

#ifndef COMMON_CONFIG_H_
#define COMMON_CONFIG_H_

namespace _obstack
{
namespace common
{
class Config
{
public:
  Config();
  static Config &instance()
  {
    static Config one;
    return one;
  }
#define DEF_CONF(type, name, ...) type name;
#include <common/config.h>
#undef DEF_CONF
};
}
}

#define CONF (common::Config::instance())

#endif  // COMMON_CONFIG_H_
