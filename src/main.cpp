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

#include <stdio.h>
#include <fstream>
#include <sstream>
#include <string.h>
#include <algorithm>
#include <getopt.h>
#include <signal.h>
#include <dirent.h>
#include <thread>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <libunwind.h>
#include <libunwind-ptrace.h>
#include "lib/macro_utils.h"
#include "lib/signal.h"
#include "common/config.h"
#include "common/log.h"
#include "common/error.h"
#include "utils/util.h"
#include "utils/defer.h"
#include "utils/color_printf.h"
#include "obstack.h"

using namespace std;
using namespace _obstack;
using namespace _obstack::common;

struct option long_options[] = {
  {"?", no_argument, nullptr, '?'},
  {"help", no_argument, nullptr, 'h'},
  {"version", no_argument, nullptr, 'v'},
  {"file", required_argument, nullptr, 'f'},
  {"log_level", required_argument, nullptr, 'l'},
  {"no_parse", no_argument, nullptr, 'n'},
  {"agg", no_argument, nullptr, 'a'},
  {"symbol_path", required_argument, nullptr, 's'},
  {"debuginfo_path", required_argument, nullptr, 'd'},
  {"no_lineno", no_argument, nullptr, 'o'},
  {"version", no_argument, nullptr, 'v'},
  {nullptr, 0, nullptr, 0}};

static void show_version()
{
  printf("obstack (2.0.4)\n");
  printf("REVISION: %s\n", REVISION);
  printf("BUILD_TIME: %s %s\n", __DATE__, __TIME__);
  printf("Copyright (c) 2022 Alipay Inc.\n");
}

static void usage_exit() {
  printf("Usage: \n");
  printf(" obstack [option(s)] [pid]\n\n");
  printf("Example: \n");
  printf(" obstack $pid\n\n");
  printf("Options: \n");
  printf(" -l, --log_level=[DEBUG|INFO|WARN|ERROR]              : Log level\n");
  printf(" -n, --no_parse                                       : Output address only\n");
  printf(" -a, --agg                                            : Aggregate backtrace\n");
  printf(" -s, --symbol_path=path                               : Binary path\n");
  printf(" -d, --debuginfo_path=path                            : Debuginfo path\n");
  printf(" -o, --no_lineno                                      : Output function name only\n");
  printf(" -t, --thread_only                                    : Process single thread only\n");
  printf(" -v, --version                                        : Output version number\n");
  exit(1);
}

static void get_options(int argc, char** argv) {
  int c;
  while ((c = getopt_long(
              argc, argv, "?onavtl:s:d:w:k:", long_options, (int *) 0)) !=
         EOF) {
    switch (c) {
    case '?': {
      usage_exit();
      break;
    }
    case 'l': {
      common::g_log_level = common::log_level_from_str(optarg);
      break;
    }
    case 's': {
      CONF.symbol_path = optarg;
      LOG(INFO, "input symbol path: %s", CONF.symbol_path);
      break;
    }
    case 'd': {
      CONF.debuginfo_path = optarg;
      LOG(INFO, "input debuginfo path: %s", CONF.debuginfo_path);
      break;
    }
    case 'n': {
      CONF.no_parse = true;
      break;
    }
    case 'a': {
      CONF.agg = true;
      break;
    }
    case 'o': {
      CONF.no_lineno = true;
      break;
    }
    case 'v': {
      show_version();
      exit(0);
      break;
    }
    case 't': {
      CONF.thread_only = true;
      break;
    }
    default: {
      usage_exit();
      break;
    }
    }
  }
  if (argc > optind) {
    CONF.pid = atoi(argv[optind]);
    LOG(INFO, "input pid: %d", CONF.pid);
  }
}

volatile sig_atomic_t interrupt = 0;
void set_interrupt(int) { interrupt = 1; }

int interrupt_signals[] = {SIGHUP, SIGINT, SIGTERM};
sigset_t interrupt_sigset;

void init_interrupt_signal_set()
{
  for (int i = 0; i < ARRAYSIZE(interrupt_signals); i++) {
    sigaddset(&interrupt_sigset, interrupt_signals[i]);
  }
}

void install_interrupt_signals()
{
  for (int i = 0; i < ARRAYSIZE(interrupt_signals); i++) {
    signal(interrupt_signals[i], set_interrupt);
  }
}

void get_th_name(int tid, char *buf, int64_t len)
{
  char file[128];
  snprintf(file, sizeof(file), "/proc/%d/comm", tid);
  FILE *fp = fopen(file, "rt");
  if (!fp) {
  } else {
    DEFER(fclose(fp));
    fgets(buf, len, fp);
  }
}

template<typename task_cb>
void iter_task(int pid, task_cb cb, bool thread_only)
{
  struct linux_dirent64 {
    ino64_t d_ino;
    off64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
  };
  char file[128];
  snprintf(file, sizeof(file), "/proc/%d/task/", pid);
  int fd = ::open(file, O_DIRECTORY |  O_RDONLY);
  if (-1 == fd) {
  } else {
    DEFER(::close(fd));
    char buf[1024];
    ssize_t nread = 0;
    do {
      nread = syscall(SYS_getdents64, fd, buf, sizeof(buf));
      if (nread < 0) {
        /* error */
      } else if (0 == nread) {
        /* end */
      } else {
        ssize_t offset = 0;
        int tid = -1;
        while (offset < nread) {
          linux_dirent64* dirent = reinterpret_cast<linux_dirent64*>(buf + offset);
          if (strcmp(dirent->d_name, ".") != 0 &&
              strcmp(dirent->d_name, "..") != 0) {
            tid = atoi(dirent->d_name);
            if (tid != pid && thread_only) {
              /* do-nothing */
            } else {
              char tname[32];
              get_th_name(tid, tname, sizeof tname);
              cb(tid, common::trim(tname));
            }
          }
          offset += dirent->d_reclen;
        }
      }
    } while (nread > 0);
  }
}

struct Task
{
  Task()
    : n_addrs_(0) {}
  bool is_valid() const { return n_addrs_ > 0; }
  int tid_;
  char tname_[32];
  ulong addrs_[256];
  int64_t n_addrs_;
  char bt_[2048];
};

bool is_pid_stopped(int pid)
{
  FILE* status_file;
  char buf[100];
  bool stopped = false;

  snprintf(buf, sizeof(buf), "/proc/%d/status", (int)pid);
  status_file = fopen(buf, "r");
  if (status_file != NULL) {
    int have_state = 0;
    while (fgets(buf, sizeof(buf), status_file)) {
      buf[strlen(buf) - 1] = '\0';
      if (strncmp(buf, "State:", 6) == 0) {
        have_state = 1;
        break;
      }
    }
    if (have_state && strstr(buf, "T") != NULL) {
      LOG(WARN, "process %d %s", pid, buf);
      stopped = true;
    }
    fclose(status_file);
  }
  return stopped;
}

int main(int argc, char** argv)
{
  int rc = 0;
  tzset();
  init_interrupt_signal_set();
  int64_t s_ts = current_time();
  if (argc <= 1) {
    usage_exit();
  }
  get_options(argc, argv);

  vector<Task*> tasks;
  auto &&task_cb = [&](int tid, char *tname) {
                     void *ptr =
                       mmap(0, sizeof(Task), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
                     auto task = new (ptr) Task;
                     task->tid_ = tid;
                     strncpy(task->tname_, tname, sizeof(task->tname_));
                     tasks.push_back(task);
                   };
  iter_task(CONF.pid, task_cb, CONF.thread_only);
  if (0 == tasks.size()) {
    LOG(WARN, "process not exist, pid: %d", CONF.pid);
    error(common::ENTRY_NOT_EXIST);
  }
  int coreprocess_pid = -1;
  /* disable interrupts while main proc waiting */
  sigprocmask(SIG_BLOCK, &interrupt_sigset, NULL);
  if ((coreprocess_pid = fork()) != 0) {
    int status;
    int w_pid = wait(&status);
    sigprocmask(SIG_UNBLOCK, &interrupt_sigset, NULL);
    lib::install_fatal_signals();

    if (-1 == w_pid) {
      rc = errno;
      LOG(ERROR, "wait failed, err: %d, errmsg: %s", errno, strerror(errno));
    } else {
      if (WIFEXITED(status)) {
        rc = WEXITSTATUS(status);
        if (rc) {
          LOG(WARN, "coreprocess exit with err %d", rc);
        }
      } else if (WIFSIGNALED(status)) {
        error(common::UNEXPECTED_ERROR, "coreprocess killed by signal %d", WTERMSIG(status));
      } else {
        error(common::UNEXPECTED_ERROR, "unhandled status: %d", status);
      }
      if (0 == rc) {
        int64_t detach_ts = current_time();
        for (auto t : tasks) {
          if (!t->is_valid()) continue;
          char *buf = t->bt_;
          int buf_len = ARRAYSIZE(t->bt_);
          int pos = 0;
          for (int i = 0; i < t->n_addrs_; i++) {
            int n = snprintf(buf + pos, buf_len - pos, "0x%lx%s", t->addrs_[i],
                             i == t->n_addrs_ - 1 ? "" : " ");
            if (n < 0 || n >= buf_len) {
              break;
            } else {
              pos += n;
            }
          }
          buf[pos] = '\0';
        }
        _obstack::ObStack os(CONF.pid);
        for (auto t : tasks) {
          if (!t->is_valid()) continue;
          os.add_bt(t->tid_, t->tname_, std::vector<ulong>(t->addrs_, t->addrs_ + t->n_addrs_),
                    string(t->bt_));
        }
        os.stack_it();
        LOG(INFO, "parse addrs finish, cost(ms): %f", (current_time() - detach_ts)/1000.0);
      }
    }
    /* warn if stopped */
    for (auto t : tasks) {
      if (is_pid_stopped(t->tid_)) {
        LOG(WARN, "attention!!! process %d is still stopped", t->tid_);
      }
    }
    LOG(INFO, "exit, cost(ms): %f", (current_time() - s_ts)/1000.0);
  } else {
    sigprocmask(SIG_UNBLOCK, &interrupt_sigset, NULL);
    install_interrupt_signals();

    /* fall in shadow */
    for (int i = 0; i < argc; i++) {
      common::inplace_reverse(argv[i]);
    }

    int task_cnt = 0;
    do {
      unw_addr_space_t as = unw_create_addr_space(&_UPT_accessors,0);;
      if (!as) {
        rc = -1;
        LOG(ERROR, "unw_create_addr_space failed");
        break;
      }
      DEFER(if (as) unw_destroy_addr_space(as));
      unw_set_caching_policy(as, UNW_CACHE_GLOBAL);

      for (int ti = 0; ti < tasks.size() && !interrupt; ti++) {
        /* ignore error for everyone*/
        DEFER(rc = 0);
        DEFER(task_cnt++);
        auto t = tasks[ti];

        /* attach */
        rc = ptrace(PTRACE_ATTACH, t->tid_);
        if (-1 == rc) {
          if (errno != ESRCH) {
            LOG(WARN, "ptrace attach failed, tid: %d, err: %d, errmsg: %s",
                t->tid_, errno, strerror(errno));
          }
          continue;
        }
        /* detach use RALL */
        DEFER(ptrace(PTRACE_DETACH, t->tid_, 0, 0));

        /* wait stop */
        rc = -1;
        int wait_loops = 10;
        while (wait_loops-- > 0) {
          int st = 0;
          int w_pid = wait4(t->tid_, &st, __WALL, NULL);
          if (-1 == w_pid) {
            rc = errno;
            LOG(WARN, "wait failed, err: %d, errmsg: %s", errno, strerror(errno));
            break;
          } else if (WIFSTOPPED(st)) {
            rc = 0;
            break;
          }
          usleep(50);
        }
        if (rc != 0) {
          LOG(ERROR, "wait failed");
          continue;
        }

        /* unwind backtrace */
        struct UPT_info *ui = (struct UPT_info *)_UPT_create(t->tid_);
        if (!ui) {
          LOG(WARN, "unw_create_addr_space failed");
          rc = -1;
          continue;
        }
        DEFER(_UPT_destroy(ui));
        unw_cursor_t c;
        unw_init_remote(&c, as, ui);
        t->n_addrs_ = 0;
        int f_limit = ARRAYSIZE(t->addrs_);
        do {
          unw_word_t uip;
          if ((rc = unw_get_reg(&c, UNW_REG_IP, &uip)) < 0) {
            LOG(WARN, "get reg failed, err: %d\n", rc);
            break;
          }
          t->addrs_[t->n_addrs_] = uip;
        } while (++t->n_addrs_ < f_limit && (rc = unw_step(&c)) > 0);
      }
      if (interrupt) {
        rc = -1;
        LOG(WARN, "interruption occurs, will exit...");
      }
    } while (0);

    if (0 == rc) {
      LOG(INFO, "all tracees detached, task_cnt: %d, cost(ms): %f",
          task_cnt, (current_time() - s_ts)/1000.0);
    }
  }

  return rc;
}
