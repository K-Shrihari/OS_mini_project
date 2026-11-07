/*
 * main.c - FR8: command-line interface, argument validation, integrated demo
 *
 * Exit codes: 0 = success / expected outcome, 1 = runtime failure,
 *             2 = usage error (bad command or argument)
 */
#include "campus.h"
#include <sys/wait.h>
#include <unistd.h>

#define LOG_FILE "campus.log"

static void usage(FILE *out)
{
    fprintf(out,
        "CampusCompute - job scheduling and resource management prototype\n"
        "\n"
        "Usage: ./campuscompute <command> [options]\n"
        "\n"
        "Commands:\n"
        "  help                         Show this message\n"
        "  sched fcfs  [-f FILE]        First-Come First-Served\n"
        "  sched rr    [-q N] [-f FILE] Round Robin (default quantum %d)\n"
        "  sched prio  [-f FILE]        Non-preemptive Priority (lower number = higher)\n"
        "  sched compare [-q N] [-f FILE]  Compare all three on the same job set\n"
        "  logger [-n ITEMS] [-b BUF]   Producer-consumer logger (pthreads) -> %s\n"
        "  ipc                          Shared memory + named semaphores (fork)\n"
        "  banker safe|unsafe           Banker's safety algorithm test cases\n"
        "  worker normal|fail|signal|all  fork()/waitpid() worker status detection\n"
        "  limit nofile|mem|cpu|all     setrlimit() demonstrations\n"
        "  demo                         Run every feature and print a summary\n"
        "\n"
        "Job file format (one job per line, '#' = comment):  id arrival burst priority\n"
        "Limits: jobs<=%d, arrival<=%d, burst 1..%d, priority 0..%d, quantum>=1\n"
        "logger: ITEMS 1..50 per producer (default 6), BUF 1..16 (default 3)\n",
        DEFAULT_QUANTUM, LOG_FILE, MAX_JOBS, MAX_TIME, MAX_BURST, MAX_PRIORITY);
}

static int bad_usage(const char *msg, const char *arg)
{
    if (arg)
        fprintf(stderr, "error: %s '%s'\n\n", msg, arg);
    else
        fprintf(stderr, "error: %s\n\n", msg);
    usage(stderr);
    return 2;
}

static int parse_sched(int argc, char **argv)
{
    int quantum = DEFAULT_QUANTUM, i;
    const char *file = NULL;
    long v;

    if (argc < 3)
        return bad_usage("'sched' needs a policy (fcfs|rr|prio|compare)", NULL);
    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
            if (parse_int_strict(argv[++i], 1, 1000, &v) != 0)
                return bad_usage("time quantum must be an integer 1..1000, got", argv[i]);
            quantum = (int)v;
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            file = argv[++i];
        } else {
            return bad_usage("unknown or incomplete option", argv[i]);
        }
    }
    return cmd_sched(argv[2], quantum, file);
}

static int parse_logger(int argc, char **argv)
{
    int items = 6, buf = 3, i;
    long v;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            if (parse_int_strict(argv[++i], 1, 50, &v) != 0)
                return bad_usage("-n must be an integer 1..50, got", argv[i]);
            items = (int)v;
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            if (parse_int_strict(argv[++i], 1, 16, &v) != 0)
                return bad_usage("-b must be an integer 1..16, got", argv[i]);
            buf = (int)v;
        } else {
            return bad_usage("unknown or incomplete option", argv[i]);
        }
    }
    return cmd_logger(items, buf, LOG_FILE);
}

/* ------------------------- integrated demo ------------------------- */
static int run_demo(void)
{
    struct { const char *name; int rc; } r[8];
    int n = 0, i, fails = 0, leftover_children;
    int status;

    banner("CampusCompute integrated demonstration");
    printf("Running every feature in sequence with repeatable inputs.\n");
    fflush(stdout);

    r[n].name = "FR1/FR2 scheduling (FCFS, RR, Priority, compare)";
    r[n].rc  = cmd_sched("fcfs", DEFAULT_QUANTUM, NULL);
    r[n].rc |= cmd_sched("rr", DEFAULT_QUANTUM, NULL);
    r[n].rc |= cmd_sched("prio", DEFAULT_QUANTUM, NULL);
    r[n].rc |= cmd_sched("compare", DEFAULT_QUANTUM, NULL);
    n++;
    r[n].name = "FR3 producer-consumer logger (pthreads)";
    r[n++].rc = cmd_logger(6, 3, LOG_FILE);
    r[n].name = "FR4 shared memory + semaphores IPC";
    r[n++].rc = cmd_ipc();
    r[n].name = "FR5 Banker's safe state";
    r[n++].rc = cmd_banker("safe");
    r[n].name = "FR5 Banker's unsafe state";
    r[n++].rc = cmd_banker("unsafe");
    r[n].name = "FR6 worker normal + simulated failures";
    r[n++].rc = cmd_worker("all");
    r[n].name = "FR7 resource limits (setrlimit)";
    r[n++].rc = cmd_limit("all");

    /* Final cleanup check: no child may remain, no un-reaped zombie. */
    errno = 0;
    leftover_children = !(waitpid(-1, &status, WNOHANG) < 0 && errno == ECHILD);
    r[n].name = "FR7 cleanup: no leftover child processes";
    r[n++].rc = leftover_children;

    banner("DEMO SUMMARY");
    for (i = 0; i < n; i++) {
        printf("  [%s] %s\n", r[i].rc == 0 ? "PASS" : "FAIL", r[i].name);
        if (r[i].rc != 0)
            fails++;
    }
    printf("\nLog file '%s' written by the logger.\n", LOG_FILE);
    printf("OVERALL: %d/%d checks passed\n", n - fails, n);
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *cmd;

    setvbuf(stdout, NULL, _IOLBF, 0);       /* keep output ordered across fork() */
    if (argc < 2)
        return bad_usage("no command given", NULL);
    cmd = argv[1];

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        usage(stdout);
        return 0;
    }
    if (strcmp(cmd, "sched") == 0)
        return parse_sched(argc, argv);
    if (strcmp(cmd, "logger") == 0)
        return parse_logger(argc, argv);
    if (strcmp(cmd, "ipc") == 0)
        return argc == 2 ? cmd_ipc() : bad_usage("'ipc' takes no arguments, got", argv[2]);
    if (strcmp(cmd, "demo") == 0)
        return argc == 2 ? run_demo() : bad_usage("'demo' takes no arguments, got", argv[2]);
    if (strcmp(cmd, "banker") == 0)
        return argc == 3 ? cmd_banker(argv[2]) : bad_usage("usage: banker safe|unsafe", NULL);
    if (strcmp(cmd, "worker") == 0)
        return argc == 3 ? cmd_worker(argv[2]) : bad_usage("usage: worker normal|fail|signal|all", NULL);
    if (strcmp(cmd, "limit") == 0)
        return argc == 3 ? cmd_limit(argv[2]) : bad_usage("usage: limit nofile|mem|cpu|all", NULL);

    return bad_usage("unknown command", cmd);
}
