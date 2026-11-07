/*
 * ipc.c - FR4: POSIX shared memory + named semaphores
 *
 * The parent (producer) sends job records to a child (consumer) through ONE
 * shared-memory slot. Two named semaphores implement the classic
 * single-slot handshake:
 *     sem_empty (init 1): slot is free   -> parent waits on it, child posts
 *     sem_full  (init 0): slot has data  -> child waits on it, parent posts
 * The child re-opens the objects BY NAME (shm_open / sem_open), proving the
 * mechanism works between independent processes.
 *
 * All object names contain the parent's PID so they cannot collide with, or
 * delete, anything that does not belong to this run.
 */
#include "campus.h"
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <unistd.h>

#define SEM_TIMEOUT_SEC 5

typedef struct {
    int  seq;
    int  job_id;
    int  burst;
    int  last;
    long total;          /* written by child at the end */
    char note[64];
} SharedBlock;

static int sem_wait_timeout(sem_t *s, int secs)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += secs;
    while (sem_timedwait(s, &ts) == -1) {
        if (errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

/* ------------------------- consumer (child) ------------------------- */
static int ipc_child(const char *shm_name, const char *empty_name, const char *full_name)
{
    int rc = 0, fd;
    long total = 0;
    SharedBlock *b;
    sem_t *se, *sf;

    fd = shm_open(shm_name, O_RDWR, 0);
    if (fd < 0) { perror("  [child] shm_open"); return 1; }
    b = mmap(NULL, sizeof *b, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (b == MAP_FAILED) { perror("  [child] mmap"); return 1; }
    se = sem_open(empty_name, 0);
    sf = sem_open(full_name, 0);
    if (se == SEM_FAILED || sf == SEM_FAILED) {
        perror("  [child] sem_open");
        munmap(b, sizeof *b);
        return 1;
    }
    printf("  [child  pid=%d] attached to shared memory and semaphores\n", getpid());
    fflush(stdout);

    for (;;) {
        if (sem_wait_timeout(sf, SEM_TIMEOUT_SEC) != 0) {
            fprintf(stderr, "  [child] timed out waiting for data: %s\n", strerror(errno));
            rc = 1;
            break;
        }
        printf("  [child  ] received #%d: job P%d burst=%d\n", b->seq, b->job_id, b->burst);
        fflush(stdout);
        total += b->burst;
        if (b->last) {
            b->total = total;
            snprintf(b->note, sizeof b->note, "child summed %d jobs", b->seq);
            sem_post(se);
            break;
        }
        sem_post(se);
    }
    sem_close(se);
    sem_close(sf);
    munmap(b, sizeof *b);
    return rc;
}

/* Count entries in /dev/shm that belong to THIS run (name contains our PID). */
static int count_run_objects(pid_t pid)
{
    DIR *d = opendir("/dev/shm");
    struct dirent *e;
    char needle[32];
    int n = 0;

    if (d == NULL)
        return -1;
    snprintf(needle, sizeof needle, "campus_");
    while ((e = readdir(d)) != NULL) {
        char pidstr[16];
        snprintf(pidstr, sizeof pidstr, "_%d", (int)pid);
        if (strstr(e->d_name, needle) && strstr(e->d_name, pidstr)) {
            printf("    found leftover: /dev/shm/%s\n", e->d_name);
            n++;
        }
    }
    closedir(d);
    return n;
}

/* ------------------------- producer (parent) ------------------------ */
int cmd_ipc(void)
{
    char shm_name[64], empty_name[64], full_name[64];
    int shm_fd = -1, rc = 0, i, status = 0;
    int shm_created = 0, se_created = 0, sf_created = 0;
    SharedBlock *blk = MAP_FAILED;
    sem_t *se = SEM_FAILED, *sf = SEM_FAILED;
    pid_t child = -1, self = getpid();
    JobSet js;
    long expected = 0;

    banner("Shared-memory IPC with named semaphores");
    snprintf(shm_name, sizeof shm_name, "/campus_shm_%d", (int)self);
    snprintf(empty_name, sizeof empty_name, "/campus_empty_%d", (int)self);
    snprintf(full_name, sizeof full_name, "/campus_full_%d", (int)self);
    jobs_load_default(&js);

    /* ---- create resources ---- */
    shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shm_fd < 0) { perror("shm_open"); rc = 1; goto cleanup; }
    shm_created = 1;
    if (ftruncate(shm_fd, sizeof(SharedBlock)) != 0) { perror("ftruncate"); rc = 1; goto cleanup; }
    blk = mmap(NULL, sizeof *blk, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (blk == MAP_FAILED) { perror("mmap"); rc = 1; goto cleanup; }
    memset(blk, 0, sizeof *blk);

    se = sem_open(empty_name, O_CREAT | O_EXCL, 0600, 1);
    if (se == SEM_FAILED) { perror("sem_open(empty)"); rc = 1; goto cleanup; }
    se_created = 1;
    sf = sem_open(full_name, O_CREAT | O_EXCL, 0600, 0);
    if (sf == SEM_FAILED) { perror("sem_open(full)"); rc = 1; goto cleanup; }
    sf_created = 1;

    printf("Created: shm %s (%zu bytes)\n", shm_name, sizeof(SharedBlock));
    printf("Created: semaphores %s (init 1), %s (init 0)\n\n", empty_name, full_name);
    fflush(stdout);

    child = fork();
    if (child < 0) { perror("fork"); rc = 1; goto cleanup; }
    if (child == 0) {
        int crc = ipc_child(shm_name, empty_name, full_name);
        fflush(stdout);
        _exit(crc);
    }

    /* ---- parent produces records ---- */
    for (i = 0; i < js.count; i++) {
        if (sem_wait_timeout(se, SEM_TIMEOUT_SEC) != 0) {
            fprintf(stderr, "  [parent] timed out waiting for free slot: %s\n", strerror(errno));
            rc = 1;
            break;
        }
        blk->seq = i + 1;
        blk->job_id = js.jobs[i].id;
        blk->burst = js.jobs[i].burst;
        blk->last = (i == js.count - 1);
        expected += js.jobs[i].burst;
        printf("  [parent pid=%d] sent     #%d: job P%d burst=%d\n",
               (int)self, blk->seq, blk->job_id, blk->burst);
        fflush(stdout);
        sem_post(sf);
    }
    if (rc != 0)
        kill(child, SIGTERM);
    if (waitpid(child, &status, 0) < 0) {
        perror("waitpid");
        rc = 1;
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "child ended abnormally (status 0x%x)\n", status);
        rc = 1;
    }
    child = -1;
    if (rc == 0) {
        printf("\n  [parent] child reports total burst = %ld (%s); expected %ld -> %s\n",
               blk->total, blk->note, expected, blk->total == expected ? "MATCH" : "MISMATCH");
        if (blk->total != expected)
            rc = 1;
    }

cleanup:
    if (child > 0) {                      /* error path: never leave a child */
        kill(child, SIGTERM);
        waitpid(child, NULL, 0);
    }
    printf("\nCleanup of resources created by this run:\n");
    if (blk != MAP_FAILED)      { munmap(blk, sizeof *blk);       printf("  munmap ........ done\n"); }
    if (shm_fd >= 0)            { close(shm_fd);                  printf("  close(shm fd) . done\n"); }
    if (shm_created)            { shm_unlink(shm_name);           printf("  shm_unlink .... %s\n", shm_name); }
    if (se != SEM_FAILED)         sem_close(se);
    if (sf != SEM_FAILED)         sem_close(sf);
    if (se_created)             { sem_unlink(empty_name);         printf("  sem_unlink .... %s\n", empty_name); }
    if (sf_created)             { sem_unlink(full_name);          printf("  sem_unlink .... %s\n", full_name); }

    /* Verification: the names must no longer exist. */
    if (shm_created) {
        int t = shm_open(shm_name, O_RDWR, 0);
        if (t >= 0) { close(t); printf("  [verify] shm STILL EXISTS\n"); rc = 1; }
        else printf("  [verify] shm_open(%s) -> %s (removed)\n", shm_name, strerror(errno));
    }
    if (se_created) {
        sem_t *t = sem_open(empty_name, 0);
        if (t != SEM_FAILED) { sem_close(t); printf("  [verify] sem STILL EXISTS\n"); rc = 1; }
        else printf("  [verify] sem_open(%s) -> %s (removed)\n", empty_name, strerror(errno));
    }
    printf("  [verify] leftover objects for this run in /dev/shm: ");
    {
        int left;
        printf("\n");
        left = count_run_objects(self);
        printf("    count = %d\n", left);
        if (left > 0)
            rc = 1;
    }
    printf("RESULT: %s\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}
