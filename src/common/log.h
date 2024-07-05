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

#ifndef COMMON_LOG_H_
#define COMMON_LOG_H_

#include <stdint.h>
#include <time.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>

namespace _obstack
{
namespace common
{

inline struct tm *localtime(const time_t *unix_sec, struct tm *result)
{
  static const int HOURS_IN_DAY = 24;
  static const int MINUTES_IN_HOUR = 60;
  static const int DAYS_FROM_UNIX_TIME = 2472632;
  static const int DAYS_FROM_YEAR = 153;
  static const int MAGIC_UNKONWN_FIRST = 146097;
  static const int MAGIC_UNKONWN_SEC = 1461;
  //use __timezone from glibc/time/tzset.c, default value is -480 for china
  const int32_t tz_minutes = static_cast<int32_t>(__timezone / 60);

  //only support time > 1970/1/1 8:0:0
  if (NULL != result && NULL != unix_sec && *unix_sec > 0) {
    result->tm_sec  = static_cast<int>((*unix_sec) % MINUTES_IN_HOUR);
    int tmp_i       = static_cast<int>((*unix_sec) / MINUTES_IN_HOUR) - tz_minutes;
    result->tm_min  = tmp_i % MINUTES_IN_HOUR;

    tmp_i          /= MINUTES_IN_HOUR;
    result->tm_hour = tmp_i % HOURS_IN_DAY;
    result->tm_mday = tmp_i / HOURS_IN_DAY;
    int tmp_a       = result->tm_mday + DAYS_FROM_UNIX_TIME;
    int tmp_b       = (tmp_a * 4 + 3) / MAGIC_UNKONWN_FIRST;
    int tmp_c       = tmp_a - (tmp_b * MAGIC_UNKONWN_FIRST) / 4;
    int tmp_d       = ((tmp_c * 4 + 3) / MAGIC_UNKONWN_SEC);
    int tmp_e       = tmp_c - (tmp_d * MAGIC_UNKONWN_SEC) / 4;
    int tmp_m       = (5 * tmp_e + 2) / DAYS_FROM_YEAR;
    result->tm_mday = tmp_e + 1 - (DAYS_FROM_YEAR * tmp_m + 2) / 5;
    result->tm_mon  = tmp_m + 2 - (tmp_m / 10) * 12;
    result->tm_year = tmp_b * 100 + tmp_d - 6700 + (tmp_m / 10);
  }
  return result;
}

struct LogLevel
{
  enum LogLevelEnum
  {
    DEBUG,
    INFO,
    WARN,
    ERROR
  };
};

inline LogLevel::LogLevelEnum log_level_from_str(const char *str)
{
  if (0 == strcasecmp(str, "DEBUG")) {
    return LogLevel::DEBUG;
  }
  if (0 == strcasecmp(str, "INFO")) {
    return LogLevel::INFO;
  }
  if (0 == strcasecmp(str, "WARN")) {
    return LogLevel::WARN;
  }
  if (0 == strcasecmp(str, "ERROR")) {
    return LogLevel::ERROR;
  }
  return LogLevel::INFO;
}

#define __FILENAME__ \
  (strrchr(__FILE__, '/') ? (strrchr(__FILE__, '/') + 1):__FILE__)

#define FMT_BEGIN "[%04d-%02d-%02d %02d:%02d:%02d.%06ld] %s [%s@%s:%d] [%ld] "
#define FMT_END "\n"
#define FMT(fmt) FMT_BEGIN fmt FMT_END

extern LogLevel::LogLevelEnum g_log_level;
extern __thread char tl_log_buf[1024];

template<typename ... Args>
inline void log_data(const char *log_level,
                     const char *function,
                     const char *filename,
                     int line,
                     const char *fmt, Args && ... args)
{
  struct timeval tv;
  (void)gettimeofday(&tv, NULL);
  struct tm tm;
  time_t t = static_cast<time_t>(tv.tv_sec);
  localtime(&t, &tm);
  int64_t tid = static_cast<int64_t>(syscall(__NR_gettid));
  ssize_t log_len = snprintf(tl_log_buf, sizeof(tl_log_buf), fmt,
                             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                             tm.tm_sec, tv.tv_usec, log_level,
                             function, filename, line,
                             tid, args...);
  common::tl_log_buf[sizeof(tl_log_buf) - 1] = '\n';
  fprintf(stderr, "%.*s", log_len ,tl_log_buf);
  fflush(stderr);
}
}
}

#define LOG(log_level, fmt, args...)                                    \
  do {                                                                  \
    if (common::LogLevel::log_level >= common::g_log_level) {           \
      common::log_data(#log_level, __FUNCTION__, __FILENAME__, __LINE__, FMT(fmt), ##args); \
    }                                                                   \
  } while (0)

#endif  // COMMON_LOG_H_
