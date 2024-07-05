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

#ifndef COMMON_COLOR_PRINTF_H_
#define COMMON_COLOR_PRINTF_H_

namespace _obstack
{
namespace common
{
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_RESET   "\x1b[0m"

#define c_printf(color, fmt, args...) \
    c_fprintf(stdout, color, fmt, ##args)

#define c_fprintf(file, color, fmt, args...) \
do { \
  if (isatty(fileno(file))) { \
    fprintf(file, color fmt COLOR_RESET, ##args); \
  } else { \
    fprintf(file, fmt, ##args); \
  } \
  fflush(stdout); \
} while(0);

}
}

#endif  // COMMON_COLOR_PRINTF_H_
