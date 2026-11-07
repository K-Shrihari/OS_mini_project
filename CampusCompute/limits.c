/*
 * limits.c - FR7: process resource limits with setrlimit()
 *
 * Each demo runs in a forked CHILD so the limit never affects the parent.
 *   nofile : RLIMIT_NOFILE = 16  -> open() fails with EMFILE
 *   mem    : RLIMIT_AS = 64 MiB  -> malloc() returns NULL (ENOMEM)
 *   cpu    : RLIMIT_CPU = 1 s    -> kernel sends SIGXCPU, child terminated
 */
#include "campus.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static int demo_nofile(void)
{
    struct rlimit rl = { 16, 16 }, got;
    int fds[64], n = 0, i, saved;

    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) { perror("  [child] setrlimit"); return 1; }
    getrlimit(RLIMIT_NOFILE, &got);
    printf("  [child] RLIMIT_NOFILE applied: soft=%lu hard=%lu\n",
           (unsigned long)got.rlim_cur, (unsigned long)got.rlim_max);
    fflush(stdout);
    while (n < 64) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0)
            break;
        fds[n++] = fd;
    }
    saved = errno;
    for (i = 0; i < n; i++)
        close(fds[i]);
    printf("  [child] opened %d extra files, then open() failed: %s\n", n, strerror(saved));
    printf("  [child] effect: only 16 descriptors allowed (3 already used by stdin/out/err)\n");
    return saved == EMFILE ? 0 : 1;
}

static int demo_mem(void)
{
    struct rlimit rl = { 64UL << 20, 64UL << 20 }, got;
    void *blocks[256];
    int n = 0, i, saved;
    const size_t chunk = 4UL << 20;

    if (setrlimit(RLIMIT_AS, &rl) != 0) { perror("  [child] setrlimit"); return 1; }
    getrlimit(RLIMIT_AS, &got);
    printf("  [child] RLIMIT_AS applied: soft=%lu MiB\n", (unsigned long)(got.rlim_cur >> 20));
    fflush(stdout);
    while (n < 256) {
        void *p = malloc(chunk);
        if (p == NULL)
            break;
        blocks[n++] = p;
    }
    saved = errno;
    for (i = 0; i < n; i++)
        free(blocks[i]);
    printf("  [child] allocated %d x 4 MiB (%d MiB), then malloc() failed: %s\n",
           n, n * 4, strerror(saved));
    printf("  [child] effect: address space capped; allocation beyond limit refused\n");
    return (n < 256 && saved == ENOMEM) ? 0 : 1;
}

static int demo_cpu(void)
{
    struct rlimit core = { 0, 0 }, rl = { 1, 2 }, got;
    volatile unsigned long x = 0;

    setrlimit(RLIMIT_CORE, &core);                  /* avoid core-dump files */
    if (setrlimit(RLIMIT_CPU, &rl) != 0) { perror("  [child] setrlimit"); return 1; }
    getrlimit(RLIMIT_CPU, &got);
    printf("  [child] RLIMIT_CPU applied: soft=%lus hard=%lus; entering busy loop...\n",
           (unsigned long)got.rlim_cur, (unsigned long)got.rlim_max);
    fflush(stdout);
    for (;;)
        x++;
    return 1;                                       /* not reached */
}

/* kind: 0 = expect clean exit(0); 1 = expect SIGXCPU */
static int run_in_child(const char *name, int (*fn)(void), int expect_sigxcpu)
{
    pid_t pid;
    int status, ok = 0;

    printf("\n--- %s ---\n", name);
    fflush(stdout);
    pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        int rc = fn();
        fflush(stdout);
        _exit(rc);
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) { perror("waitpid"); return 1; }
    }
    if (WIFEXITED(status)) {
        printf("  [parent] child exit status = %d\n", WEXITSTATUS(status));
        ok = !expect_sigxcpu && WEXITSTATUS(status) == 0;
    } else if (WIFSIGNALED(status)) {
        printf("  [parent] child terminated by signal %d (%s)\n",
               WTERMSIG(status), strsignal(WTERMSIG(status)));
        ok = expect_sigxcpu && WTERMSIG(status) == SIGXCPU;
        if (ok)
            printf("  [parent] effect: CPU-time limit exceeded -> kernel delivered SIGXCPU\n");
    }
    printf("  [parent] limit applied only inside the child; observed effect as expected: %s\n",
           ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int cmd_limit(const char *which)
{
    int rc = 0, all = (strcmp(which, "all") == 0);

    if (!all && strcmp(which, "nofile") && strcmp(which, "mem") && strcmp(which, "cpu")) {
        fprintf(stderr, "error: unknown limit '%s' (use nofile|mem|cpu|all)\n", which);
        return 2;
    }
    banner("Resource limits (setrlimit)");
    if (all || strcmp(which, "nofile") == 0)
        rc |= run_in_child("RLIMIT_NOFILE demo", demo_nofile, 0);
    if (all || strcmp(which, "mem") == 0)
        rc |= run_in_child("RLIMIT_AS demo", demo_mem, 0);
    if (all || strcmp(which, "cpu") == 0)
        rc |= run_in_child("RLIMIT_CPU demo (takes ~1 s)", demo_cpu, 1);
    printf("\nRESULT: %s\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}
