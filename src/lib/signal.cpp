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

#include "lib/signal.h"
#include <stdlib.h>

namespace _obstack
{
namespace lib
{
static void defalut_handler(int sig)
{
  signal(sig, SIG_DFL);
  raise(sig);
}

__thread sighandler_t tl_signal_handler = defalut_handler;

static const int SIG_SET[] = {SIGABRT, SIGBUS, SIGFPE, SIGSEGV};

static void handler(int sig, siginfo_t *s, void *p)
{
  if (tl_signal_handler != nullptr) {
    tl_signal_handler(sig);
  }
}

void install_fatal_signals()
{
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO | SA_RESTART | SA_NODEFER | SA_ONSTACK;
  sa.sa_sigaction = handler;
  sigemptyset(&sa.sa_mask);
  for (int i = 0; i < sizeof(SIG_SET)/sizeof(SIG_SET[0]); i++) {
    if (-1 == sigaction(SIG_SET[i], &sa, nullptr)) {
      abort();
    }
  }
}

}
}
