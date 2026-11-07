# CampusCompute

Intelligent job scheduling and resource management prototype.
Operating Systems (UE25CS242A) - Level 3 Mini-Project. Written in C for Linux / Ubuntu / WSL Ubuntu.

## Prerequisites
- Ubuntu (or WSL Ubuntu) with `gcc` and `make`:
  `sudo apt update && sudo apt install -y build-essential`
- POSIX threads, POSIX shared memory and named semaphores (all part of glibc; no extra packages).
- WSL users: run inside the Linux filesystem (e.g. `~/campuscompute`), not `/mnt/c`, so `/dev/shm` works normally.

## Build
```
make            # builds ./campuscompute
make clean      # removes objects, executable and campus.log (only files this project creates)
make test       # runs 19 smoke tests (tests/run_tests.sh)
```
Manual build (without make):
```
gcc -std=gnu11 -Wall -Wextra -O2 -pthread -o campuscompute main.c util.c scheduler.c logger.c ipc.c banker.c worker.c limits.c -pthread -lrt
```

## Commands
| Command | Feature | Req. |
|---|---|---|
| `./campuscompute help` | usage / command list | FR8 |
| `./campuscompute sched fcfs [-f FILE]` | First-Come First-Served | FR1, FR2 |
| `./campuscompute sched rr [-q N] [-f FILE]` | Round Robin (default quantum 4) | FR2 |
| `./campuscompute sched prio [-f FILE]` | Non-preemptive priority | FR2 |
| `./campuscompute sched compare [-q N] [-f FILE]` | all three on the same job set | FR2 |
| `./campuscompute logger [-n ITEMS] [-b BUF]` | pthread producer-consumer logger, writes `campus.log` | FR3 |
| `./campuscompute ipc` | shared memory + named semaphores between parent/child | FR4 |
| `./campuscompute banker safe` / `unsafe` | Banker's safety algorithm | FR5 |
| `./campuscompute worker normal\|fail\|signal\|all` | fork()/waitpid() status detection | FR6 |
| `./campuscompute limit nofile\|mem\|cpu\|all` | setrlimit() demonstrations | FR7 |
| `./campuscompute demo` | runs everything, prints PASS/FAIL summary | FR1-FR8 |

Exit codes: `0` success / expected outcome, `1` runtime failure, `2` usage error.

## Scheduling rules (printed on every run)
- **Formulas:** Turnaround = Completion - Arrival; Waiting = Turnaround - Burst.
- **FCFS:** earlier arrival first; equal arrival -> lower job ID.
- **Priority (non-preemptive):** lower number = higher priority; tie -> earlier arrival, then lower job ID.
- **Round Robin:** default time quantum **4** (change with `-q`). FIFO ready queue; a job that arrives at the
  same instant a quantum expires is queued **before** the preempted job.
- CPU idle gaps are shown as `IDLE`.

## Job file format
One job per line: `id arrival burst priority`; `#` starts a comment. Limits: up to 32 jobs, arrival 0..1000,
burst 1..100, priority 0..99, unique positive IDs. Samples are in `tests/`:
`jobs_sample.txt`, `jobs_idle.txt` (idle gap), `jobs_bad.txt` (malformed), `jobs_dup.txt` (duplicate ID).
Example: `./campuscompute sched rr -q 3 -f tests/jobs_sample.txt`

## Design notes
- **Logger (FR3):** circular buffer, one mutex + `not_full` / `not_empty` condition variables, waits in `while` loops.
  2 producers, 2 consumers. Shutdown: producers are joined, `done` is set and broadcast, consumers drain the queue
  and exit; every thread is joined. A second mutex serialises log-file/console output.
- **IPC (FR4):** `shm_open` + `ftruncate` + `mmap` for a one-slot mailbox, two named semaphores
  (`sem_empty`, `sem_full`) for the handshake. Names include the PID (`/campus_shm_<pid>` etc.). The child re-opens
  the objects by name. Cleanup (`munmap`, `close`, `shm_unlink`, `sem_close`, `sem_unlink`) runs on every path and
  is verified (`shm_open` / `sem_open` must now fail with ENOENT).
- **Banker (FR5):** `safe` dataset gives sequence P1 -> P3 -> P4 -> P0 -> P2; `unsafe` (Available = 0 1 1) is blocked
  after P3 and P1 finish.
- **Workers (FR6):** failures are **simulated** (exit status 3, or the worker sends itself SIGTERM) and labelled so.
- **Limits (FR7):** each demo runs in a forked child so the parent is unaffected:
  `RLIMIT_NOFILE=16` -> `open()` fails with EMFILE; `RLIMIT_AS=64 MiB` -> `malloc()` returns NULL (ENOMEM);
  `RLIMIT_CPU=1s` -> kernel kills the child with SIGXCPU.

## Cleanup / checking nothing is left behind
```
ls /dev/shm | grep campus     # should print nothing
ps aux | grep campuscompute   # only the grep line itself
make clean                    # removes build products and campus.log
```
The program only creates/removes objects whose names start with `campus_` and contain its own PID.

## Files
`main.c` CLI + demo | `scheduler.c` FR1/FR2 | `logger.c` FR3 | `ipc.c` FR4 | `banker.c` FR5 |
`worker.c` FR6 | `limits.c` FR7 | `util.c` helpers | `campus.h` shared header | `Makefile` | `tests/`
