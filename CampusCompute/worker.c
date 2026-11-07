/*
 * worker.c - FR6: worker creation and failure detection
 *
 * The parent fork()s worker processes and collects each one's termination
 * status with waitpid(-1, ...), decoding it with WIFEXITED/WEXITSTATUS and
 * WIFSIGNALED/WTERMSIG.
 *
 * NOTE: every failure here is SIMULATED on purpose (the child deliberately
 * exits with status 3 or sends itself SIGTERM). These are not real OS faults.
 */
#include "campus.h"
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum { W_NORMAL, W_FAIL_EXIT, W_FAIL_SIGNAL } WMode;

#define SIM_EXIT_CODE 3

static const char *mode_text(WMode m)
{
    switch (m) {
    case W_NORMAL:      return "normal completion expected";
    case W_FAIL_EXIT:   return "SIMULATED failure: exit status 3";
    default:            return "SIMULATED failure: killed by SIGTERM";
    }
}

static void worker_body(int idx, WMode m)
{
    printf("  [worker %d pid=%d] started (%s)\n", idx, (int)getpid(), mode_text(m));
    fflush(stdout);
    usleep(100000u * (unsigned)(idx + 1));          /* pretend to work */
    switch (m) {
    case W_NORMAL:
        printf("  [worker %d pid=%d] work finished, exiting with 0\n", idx, (int)getpid());
        fflush(stdout);
        _exit(0);
    case W_FAIL_EXIT:
        printf("  [worker %d pid=%d] *** SIMULATED FAILURE *** exiting with status %d\n",
               idx, (int)getpid(), SIM_EXIT_CODE);
        fflush(stdout);
        _exit(SIM_EXIT_CODE);
    default:
        printf("  [worker %d pid=%d] *** SIMULATED FAILURE *** raising SIGTERM on itself\n",
               idx, (int)getpid());
        fflush(stdout);
        raise(SIGTERM);
        _exit(99);                                  /* not reached */
    }
}

/* Returns 1 if the observed status is exactly what the mode should produce. */
static int report(int idx, pid_t pid, WMode m, int status)
{
    int matched = 0;

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        printf("  [parent] worker %d (pid %d) exited with status %d -> ", idx, (int)pid, code);
        if (code == 0) {
            printf("NORMAL COMPLETION\n");
            matched = (m == W_NORMAL);
        } else {
            printf("FAILURE DETECTED (simulated, non-zero exit)\n");
            matched = (m == W_FAIL_EXIT && code == SIM_EXIT_CODE);
        }
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("  [parent] worker %d (pid %d) killed by signal %d (%s) -> "
               "FAILURE DETECTED (simulated, abnormal termination)\n",
               idx, (int)pid, sig, strsignal(sig));
        matched = (m == W_FAIL_SIGNAL && sig == SIGTERM);
    } else {
        printf("  [parent] worker %d (pid %d) unexpected status 0x%x\n", idx, (int)pid, status);
    }
    return matched;
}

int cmd_worker(const char *mode)
{
    WMode modes[3];
    pid_t pids[3];
    int n = 0, i, rc = 0, remaining = 0, ok_count = 0;

    if (strcmp(mode, "normal") == 0)      { modes[n++] = W_NORMAL; }
    else if (strcmp(mode, "fail") == 0)   { modes[n++] = W_FAIL_EXIT; }
    else if (strcmp(mode, "signal") == 0) { modes[n++] = W_FAIL_SIGNAL; }
    else if (strcmp(mode, "all") == 0) {
        modes[n++] = W_NORMAL;
        modes[n++] = W_FAIL_EXIT;
        modes[n++] = W_FAIL_SIGNAL;
    } else {
        fprintf(stderr, "error: unknown worker mode '%s' (use normal|fail|signal|all)\n", mode);
        return 2;
    }

    banner("Worker processes: fork() + waitpid()");
    printf("Parent pid=%d launching %d worker(s). Failures below are SIMULATED.\n\n",
           (int)getpid(), n);
    fflush(stdout);

    for (i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            rc = 1;
        } else if (pids[i] == 0) {
            worker_body(i + 1, modes[i]);               /* never returns */
        } else {
            remaining++;
        }
    }
    while (remaining > 0) {
        int status;
        pid_t w = waitpid(-1, &status, 0);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            perror("waitpid");
            rc = 1;
            break;
        }
        for (i = 0; i < n; i++)
            if (pids[i] == w) {
                if (report(i + 1, w, modes[i], status))
                    ok_count++;
                else
                    rc = 1;
                break;
            }
        remaining--;
    }
    printf("\nSummary: %d/%d workers ended exactly as expected; all children reaped.\n", ok_count, n);
    printf("RESULT: %s\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}
