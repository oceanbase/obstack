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

#ifndef COMMON_DEFER_H_
#define COMMON_DEFER_H_

#include <functional>
#include <vector>
#include <utility>

namespace _obstack
{
namespace common
{
template <typename FnType>
class ScopedLambda {
 public:
  explicit ScopedLambda(FnType fn) : fn_(std::move(fn)), active_(true) { }
  // Default movable.
  ScopedLambda(ScopedLambda&&) = default;
  ScopedLambda& operator=(ScopedLambda&&) = default;
  // Non-copyable. In particular, there is no good reasoning about which copy
  // remains active.
  ScopedLambda(const ScopedLambda&) = delete;
  ScopedLambda& operator=(const ScopedLambda&) = delete;
  ~ScopedLambda() {
    if (active_) fn_();
  }
  void run_and_expire() {
    if (active_) fn_();
    active_ = false;
  }
  void activate() { active_ = true; }
  void deactivate() { active_ = false; }

 private:
  FnType fn_;
  bool active_ = true;
};

template <typename FnType>
ScopedLambda<FnType> MakeScopedLambda(FnType fn) {
  return ScopedLambda<FnType>(std::move(fn));
}

#define TOKEN_PASTE(x, y) x ## y
#define TOKEN_PASTE2(x, y) TOKEN_PASTE(x, y)
#define SCOPE_UNIQUE_NAME(name) TOKEN_PASTE2(name, __LINE__)
#define DEFER(...)                                                  \
    auto SCOPE_UNIQUE_NAME(defer_varname) =                         \
        common::MakeScopedLambda([&] {                            \
            __VA_ARGS__;                                            \
        });
#define NAMED_DEFER(name, ...)                                      \
    auto name = common::MakeScopedLambda([&] { __VA_ARGS__; })
#define DEFER_SET(name)                                             \
    common::ScopedLambdaSet name;
}
}

#endif  // COMMON_DEFER_H_
