/*
 * logger.c - FR3: concurrent activity logger (bounded producer-consumer)
 *
 * Design
 *   - Circular buffer of fixed capacity holding log-event strings.
 *   - ONE mutex (mtx) protects the buffer indices/count/done flag.
 *   - Condition variable not_full  : producers wait while buffer is full.
 *   - Condition variable not_empty : consumers wait while buffer is empty.
 *   - Waits are always inside a while-loop (guards against spurious wakeups).
 *   - A second mutex (out_mtx) serialises writes to the log file/stdout.
 *     Lock order is always mtx -> out_mtx (consumers never hold mtx while
 *     taking out_mtx), so no deadlock is possible.
 *   - Orderly shutdown: after all producers are joined, main sets 'done'
 *     and broadcasts not_empty. Consumers drain the remaining items and
 *     exit only when (count == 0 && done). Every thread is pthread_join'ed.
 */
#include "campus.h"
#include <pthread.h>
#include <unistd.h>

#define MSG_LEN        128
#define NUM_PRODUCERS  2
#define NUM_CONSUMERS  2

typedef struct {
    char (*msgs)[MSG_LEN];
    int cap, head, tail, count, done, next_seq;
    pthread_mutex_t mtx;
    pthread_cond_t not_full, not_empty;
    pthread_mutex_t out_mtx;
    FILE *log;
} LogCtx;

typedef struct {
    LogCtx *ctx;
    int id;
    int items;
    int handled;
} WorkerArg;

static void *producer_thread(void *p)
{
    WorkerArg *a = p;
    LogCtx *c = a->ctx;
    int i;

    for (i = 0; i < a->items; i++) {
        pthread_mutex_lock(&c->mtx);
        while (c->count == c->cap) {                 /* buffer FULL */
            pthread_mutex_lock(&c->out_mtx);
            printf("  [producer P%d] buffer FULL -> waiting on not_full\n", a->id);
            pthread_mutex_unlock(&c->out_mtx);
            pthread_cond_wait(&c->not_full, &c->mtx);
        }
        int seq = ++c->next_seq;
        snprintf(c->msgs[c->tail], MSG_LEN,
                 "seq=%03d producer=P%d event=JOB_SUBMITTED job=J%d%02d",
                 seq, a->id, a->id, i + 1);
        pthread_mutex_lock(&c->out_mtx);
        printf("  [producer P%d] enqueued  seq=%03d  (buffer %d/%d)\n",
               a->id, seq, c->count + 1, c->cap);
        pthread_mutex_unlock(&c->out_mtx);
        c->tail = (c->tail + 1) % c->cap;
        c->count++;
        a->handled++;
        pthread_cond_signal(&c->not_empty);
        pthread_mutex_unlock(&c->mtx);
        usleep(15000u * (unsigned)a->id);            /* simulate work */
    }
    return NULL;
}

static void *consumer_thread(void *p)
{
    WorkerArg *a = p;
    LogCtx *c = a->ctx;
    char msg[MSG_LEN], ts[32];

    for (;;) {
        pthread_mutex_lock(&c->mtx);
        while (c->count == 0 && !c->done)            /* buffer EMPTY */
            pthread_cond_wait(&c->not_empty, &c->mtx);
        if (c->count == 0 && c->done) {              /* drained + shutdown */
            pthread_mutex_unlock(&c->mtx);
            break;
        }
        strcpy(msg, c->msgs[c->head]);
        c->head = (c->head + 1) % c->cap;
        c->count--;
        pthread_cond_signal(&c->not_full);
        pthread_mutex_unlock(&c->mtx);

        pthread_mutex_lock(&c->out_mtx);
        timestamp(ts, sizeof ts);
        fprintf(c->log, "[%s] consumer=C%d %s\n", ts, a->id, msg);
        fflush(c->log);
        printf("  [consumer C%d] logged    %s\n", a->id, msg);
        a->handled++;
        pthread_mutex_unlock(&c->out_mtx);
        usleep(40000);                               /* slow consumer */
    }
    pthread_mutex_lock(&c->out_mtx);
    printf("  [consumer C%d] queue drained, exiting (handled %d)\n", a->id, a->handled);
    pthread_mutex_unlock(&c->out_mtx);
    return NULL;
}

static int count_lines(const char *path)
{
    FILE *fp = fopen(path, "r");
    int n = 0, ch;

    if (fp == NULL)
        return -1;
    while ((ch = fgetc(fp)) != EOF)
        if (ch == '\n')
            n++;
    fclose(fp);
    return n;
}

int cmd_logger(int items, int bufsize, const char *logpath)
{
    LogCtx ctx;
    WorkerArg prod[NUM_PRODUCERS], cons[NUM_CONSUMERS];
    pthread_t pt[NUM_PRODUCERS], ct[NUM_CONSUMERS];
    int pcreated = 0, ccreated = 0, i, rc = 0, produced = 0, consumed = 0;
    int lines, expected;

    banner("Concurrent logger (producer-consumer)");
    memset(&ctx, 0, sizeof ctx);
    ctx.cap = bufsize;
    ctx.msgs = calloc((size_t)bufsize, MSG_LEN);
    if (ctx.msgs == NULL) {
        perror("calloc");
        return 1;
    }
    ctx.log = fopen(logpath, "w");
    if (ctx.log == NULL) {
        fprintf(stderr, "error: cannot open log file '%s': %s\n", logpath, strerror(errno));
        free(ctx.msgs);
        return 1;
    }
    pthread_mutex_init(&ctx.mtx, NULL);
    pthread_mutex_init(&ctx.out_mtx, NULL);
    pthread_cond_init(&ctx.not_full, NULL);
    pthread_cond_init(&ctx.not_empty, NULL);

    fprintf(ctx.log, "# CampusCompute activity log (producers=%d consumers=%d "
            "items/producer=%d buffer=%d)\n", NUM_PRODUCERS, NUM_CONSUMERS, items, bufsize);
    printf("Producers=%d  Consumers=%d  items/producer=%d  buffer capacity=%d\n",
           NUM_PRODUCERS, NUM_CONSUMERS, items, bufsize);
    printf("Log file: %s\n\n", logpath);

    for (i = 0; i < NUM_CONSUMERS; i++) {
        cons[i] = (WorkerArg){ .ctx = &ctx, .id = i + 1 };
        if (pthread_create(&ct[i], NULL, consumer_thread, &cons[i]) != 0) {
            fprintf(stderr, "error: pthread_create (consumer) failed\n");
            rc = 1;
            break;
        }
        ccreated++;
    }
    if (rc == 0) {
        for (i = 0; i < NUM_PRODUCERS; i++) {
            prod[i] = (WorkerArg){ .ctx = &ctx, .id = i + 1, .items = items };
            if (pthread_create(&pt[i], NULL, producer_thread, &prod[i]) != 0) {
                fprintf(stderr, "error: pthread_create (producer) failed\n");
                rc = 1;
                break;
            }
            pcreated++;
        }
    }

    /* Orderly termination: producers first, then signal shutdown. */
    for (i = 0; i < pcreated; i++) {
        pthread_join(pt[i], NULL);
        produced += prod[i].handled;
        printf("  [main] joined producer P%d\n", prod[i].id);
    }
    pthread_mutex_lock(&ctx.mtx);
    ctx.done = 1;
    pthread_cond_broadcast(&ctx.not_empty);
    pthread_mutex_unlock(&ctx.mtx);
    for (i = 0; i < ccreated; i++) {
        pthread_join(ct[i], NULL);
        consumed += cons[i].handled;
        printf("  [main] joined consumer C%d\n", cons[i].id);
    }

    fclose(ctx.log);
    pthread_mutex_destroy(&ctx.mtx);
    pthread_mutex_destroy(&ctx.out_mtx);
    pthread_cond_destroy(&ctx.not_full);
    pthread_cond_destroy(&ctx.not_empty);
    free(ctx.msgs);

    lines = count_lines(logpath);
    expected = NUM_PRODUCERS * items + 1;            /* +1 header line */
    printf("\nSummary: produced=%d consumed=%d log lines=%d (expected %d)\n",
           produced, consumed, lines, expected);
    if (rc == 0 && produced == consumed && produced == NUM_PRODUCERS * items &&
        lines == expected) {
        printf("RESULT: PASS - no events lost, all threads joined cleanly\n");
        return 0;
    }
    printf("RESULT: FAIL\n");
    return 1;
}
