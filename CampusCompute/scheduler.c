/*
 * scheduler.c - FR1 (job management) and FR2 (CPU scheduling)
 *
 * Policies : FCFS, Round Robin, non-preemptive Priority
 * Metrics  : completion, turnaround (= CT - AT), waiting (= TAT - burst)
 *
 * Tie-breaking rules (printed with every run):
 *   FCFS     : earlier arrival first; equal arrival -> lower job ID
 *   Priority : LOWER number = HIGHER priority; tie -> earlier arrival,
 *              then lower job ID. Non-preemptive.
 *   RR       : FIFO ready queue. A job that arrives at the same instant a
 *              quantum expires is queued BEFORE the preempted job.
 */
#include "campus.h"

/* ------------------------------------------------------------------ */
/* FR1: job loading and validation                                     */
/* ------------------------------------------------------------------ */

static void job_init(Job *j, int id, int at, int bt, int pr)
{
    j->id = id;
    j->arrival = at;
    j->burst = bt;
    j->priority = pr;
    j->remaining = bt;
    j->start = -1;
    j->completion = j->turnaround = j->waiting = 0;
    j->status = JOB_NEW;
}

/* Fixed, repeatable test set. P4 and P5 share priority 2 to exercise
 * the priority tie-break rule. {id, arrival, burst, priority} */
int jobs_load_default(JobSet *js)
{
    static const int d[][4] = {
        {1, 0, 5, 3}, {2, 1, 3, 1}, {3, 2, 8, 4}, {4, 3, 6, 2}, {5, 4, 2, 2}
    };
    size_t i;

    memset(js, 0, sizeof *js);
    js->count = (int)(sizeof d / sizeof d[0]);
    for (i = 0; i < (size_t)js->count; i++)
        job_init(&js->jobs[i], d[i][0], d[i][1], d[i][2], d[i][3]);
    return 0;
}

/* File format: one job per line:  id arrival burst priority
 * '#' starts a comment; blank lines are ignored. */
int jobs_load_file(JobSet *js, const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[256];
    int lineno = 0;

    memset(js, 0, sizeof *js);
    if (fp == NULL) {
        fprintf(stderr, "error: cannot open job file '%s': %s\n",
                path, strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof line, fp) != NULL) {
        char *hash, *tok, *save = NULL;
        long v[4];
        int n = 0;

        lineno++;
        if (strchr(line, '\n') == NULL && !feof(fp)) {
            fprintf(stderr, "error: %s:%d: line too long\n", path, lineno);
            fclose(fp);
            return -1;
        }
        if ((hash = strchr(line, '#')) != NULL)
            *hash = '\0';
        for (tok = strtok_r(line, " \t\r\n", &save); tok != NULL;
             tok = strtok_r(NULL, " \t\r\n", &save)) {
            if (n >= 4 || parse_int_strict(tok, -1000000, 1000000, &v[n]) != 0) {
                fprintf(stderr, "error: %s:%d: malformed field '%s' "
                        "(expected: id arrival burst priority)\n",
                        path, lineno, tok);
                fclose(fp);
                return -1;
            }
            n++;
        }
        if (n == 0)
            continue;
        if (n != 4) {
            fprintf(stderr, "error: %s:%d: expected 4 integers, got %d\n",
                    path, lineno, n);
            fclose(fp);
            return -1;
        }
        if (js->count >= MAX_JOBS) {
            fprintf(stderr, "error: too many jobs (max %d)\n", MAX_JOBS);
            fclose(fp);
            return -1;
        }
        job_init(&js->jobs[js->count++], (int)v[0], (int)v[1], (int)v[2], (int)v[3]);
    }
    fclose(fp);
    return 0;
}

int jobs_validate(const JobSet *js)
{
    int i, k;

    if (js->count < 1 || js->count > MAX_JOBS) {
        fprintf(stderr, "error: job count must be 1..%d (got %d)\n",
                MAX_JOBS, js->count);
        return -1;
    }
    for (i = 0; i < js->count; i++) {
        const Job *j = &js->jobs[i];
        if (j->id <= 0) {
            fprintf(stderr, "error: job ID must be > 0 (got %d)\n", j->id);
            return -1;
        }
        if (j->arrival < 0 || j->arrival > MAX_TIME) {
            fprintf(stderr, "error: P%d arrival must be 0..%d (got %d)\n",
                    j->id, MAX_TIME, j->arrival);
            return -1;
        }
        if (j->burst < 1 || j->burst > MAX_BURST) {
            fprintf(stderr, "error: P%d burst must be 1..%d (got %d)\n",
                    j->id, MAX_BURST, j->burst);
            return -1;
        }
        if (j->priority < 0 || j->priority > MAX_PRIORITY) {
            fprintf(stderr, "error: P%d priority must be 0..%d (got %d)\n",
                    j->id, MAX_PRIORITY, j->priority);
            return -1;
        }
        for (k = 0; k < i; k++) {
            if (js->jobs[k].id == j->id) {
                fprintf(stderr, "error: duplicate job ID %d\n", j->id);
                return -1;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* FR2: scheduling engines                                             */
/* ------------------------------------------------------------------ */

static const char *algo_name(SchedAlgo a)
{
    switch (a) {
    case ALGO_FCFS:     return "FCFS";
    case ALGO_RR:       return "Round Robin";
    default:            return "Priority (non-preemptive)";
    }
}

static const char *tie_rule(SchedAlgo a)
{
    switch (a) {
    case ALGO_FCFS:
        return "earlier arrival first; equal arrival -> lower job ID";
    case ALGO_RR:
        return "FIFO ready queue; a job arriving exactly when a quantum "
               "expires is queued BEFORE the preempted job";
    default:
        return "lower number = higher priority; tie -> earlier arrival, "
               "then lower job ID";
    }
}

/* Append a Gantt segment, merging with the previous one when contiguous. */
static void add_seg(SchedResult *r, int id, int s, int e)
{
    if (e <= s)
        return;
    if (r->nseg > 0 && r->seg[r->nseg - 1].job_id == id &&
        r->seg[r->nseg - 1].end == s) {
        r->seg[r->nseg - 1].end = e;
        return;
    }
    if (r->nseg >= MAX_SEGMENTS) {
        r->overflow = 1;
        return;
    }
    r->seg[r->nseg].job_id = id;
    r->seg[r->nseg].start = s;
    r->seg[r->nseg].end = e;
    r->nseg++;
}

static int arrival_cmp(const JobSet *js, int a, int b)
{
    if (js->jobs[a].arrival != js->jobs[b].arrival)
        return js->jobs[a].arrival - js->jobs[b].arrival;
    return js->jobs[a].id - js->jobs[b].id;
}

/* order[] = job indices sorted by (arrival, id) using insertion sort. */
static void sort_by_arrival(const JobSet *js, int *order)
{
    int i, j, key;

    for (i = 0; i < js->count; i++)
        order[i] = i;
    for (i = 1; i < js->count; i++) {
        key = order[i];
        for (j = i - 1; j >= 0 && arrival_cmp(js, order[j], key) > 0; j--)
            order[j + 1] = order[j];
        order[j + 1] = key;
    }
}

static void finish_job(Job *j, int t)
{
    j->remaining = 0;
    j->completion = t;
    j->status = JOB_DONE;
}

static void run_fcfs(SchedResult *r)
{
    int order[MAX_JOBS], k, t = 0;

    sort_by_arrival(&r->jobs, order);
    for (k = 0; k < r->jobs.count; k++) {
        Job *j = &r->jobs.jobs[order[k]];
        if (t < j->arrival) {
            add_seg(r, -1, t, j->arrival);
            t = j->arrival;
        }
        j->status = JOB_RUNNING;
        j->start = t;
        add_seg(r, j->id, t, t + j->burst);
        t += j->burst;
        finish_job(j, t);
    }
    r->makespan = t;
}

static void run_priority(SchedResult *r)
{
    JobSet *js = &r->jobs;
    int t = 0, done = 0, i;

    while (done < js->count) {
        int best = -1;
        for (i = 0; i < js->count; i++) {
            const Job *c = &js->jobs[i];
            if (c->status == JOB_DONE || c->arrival > t)
                continue;
            if (best < 0) { best = i; continue; }
            const Job *b = &js->jobs[best];
            if (c->priority < b->priority ||
                (c->priority == b->priority &&
                 (c->arrival < b->arrival ||
                  (c->arrival == b->arrival && c->id < b->id))))
                best = i;
        }
        if (best < 0) {                       /* CPU idle until next arrival */
            int next = MAX_TIME + 1;
            for (i = 0; i < js->count; i++)
                if (js->jobs[i].status != JOB_DONE && js->jobs[i].arrival < next)
                    next = js->jobs[i].arrival;
            add_seg(r, -1, t, next);
            t = next;
            continue;
        }
        Job *j = &js->jobs[best];
        j->start = t;
        add_seg(r, j->id, t, t + j->burst);
        t += j->burst;
        finish_job(j, t);
        done++;
    }
    r->makespan = t;
}

static void run_rr(SchedResult *r)
{
    JobSet *js = &r->jobs;
    int order[MAX_JOBS], queue[MAX_JOBS + 1];
    int qh = 0, qt = 0, qn = 0, next = 0, t = 0, done = 0;
    const int qcap = MAX_JOBS + 1;

    sort_by_arrival(js, order);

    /* admit(): move every job that has arrived by time t into the queue */
#define ADMIT()                                                          \
    while (next < js->count && js->jobs[order[next]].arrival <= t) {     \
        queue[qt] = order[next++];                                       \
        qt = (qt + 1) % qcap;                                            \
        qn++;                                                            \
    }

    while (done < js->count) {
        ADMIT();
        if (qn == 0) {                        /* idle until next arrival */
            int na = js->jobs[order[next]].arrival;
            add_seg(r, -1, t, na);
            t = na;
            continue;
        }
        int idx = queue[qh];
        qh = (qh + 1) % qcap;
        qn--;
        Job *j = &js->jobs[idx];
        int slice = j->remaining < r->quantum ? j->remaining : r->quantum;

        if (j->start < 0)
            j->start = t;
        add_seg(r, j->id, t, t + slice);
        t += slice;
        j->remaining -= slice;
        ADMIT();                              /* new arrivals go first */
        if (j->remaining == 0) {
            finish_job(j, t);
            done++;
        } else {
            queue[qt] = idx;
            qt = (qt + 1) % qcap;
            qn++;
        }
    }
#undef ADMIT
    r->makespan = t;
}

int sched_run(const JobSet *in, SchedAlgo algo, int quantum, SchedResult *r)
{
    int i, prev = -1;
    double sct = 0, stat = 0, swt = 0;

    memset(r, 0, sizeof *r);
    if (jobs_validate(in) != 0)
        return -1;
    if (algo == ALGO_RR && quantum < 1) {
        fprintf(stderr, "error: time quantum must be >= 1\n");
        return -1;
    }
    r->algo = algo;
    r->quantum = quantum;
    r->jobs = *in;
    for (i = 0; i < r->jobs.count; i++)
        job_init(&r->jobs.jobs[i], in->jobs[i].id, in->jobs[i].arrival,
                 in->jobs[i].burst, in->jobs[i].priority);

    switch (algo) {
    case ALGO_FCFS:     run_fcfs(r);     break;
    case ALGO_RR:       run_rr(r);       break;
    default:            run_priority(r); break;
    }
    if (r->overflow) {
        fprintf(stderr, "error: timeline too long (max %d segments)\n",
                MAX_SEGMENTS);
        return -1;
    }
    for (i = 0; i < r->jobs.count; i++) {
        Job *j = &r->jobs.jobs[i];
        j->turnaround = j->completion - j->arrival;
        j->waiting = j->turnaround - j->burst;
        sct += j->completion;
        stat += j->turnaround;
        swt += j->waiting;
    }
    r->avg_ct = sct / r->jobs.count;
    r->avg_tat = stat / r->jobs.count;
    r->avg_wt = swt / r->jobs.count;
    for (i = 0; i < r->nseg; i++) {
        if (r->seg[i].job_id < 0)
            continue;
        if (prev != -1 && prev != r->seg[i].job_id)
            r->context_switches++;
        prev = r->seg[i].job_id;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

static void print_job_set(const JobSet *js)
{
    int i;
    printf("Input jobs:\n  %-5s %-8s %-6s %-8s\n", "Job", "Arrival", "Burst", "Priority");
    for (i = 0; i < js->count; i++)
        printf("  P%-4d %-8d %-6d %-8d\n", js->jobs[i].id, js->jobs[i].arrival,
               js->jobs[i].burst, js->jobs[i].priority);
}

static void print_timeline(const SchedResult *r)
{
    int i;
    printf("Execution order / Gantt timeline:\n  ");
    for (i = 0; i < r->nseg; i++) {
        if (r->seg[i].job_id < 0)
            printf("| IDLE (%d-%d) ", r->seg[i].start, r->seg[i].end);
        else
            printf("| P%d (%d-%d) ", r->seg[i].job_id, r->seg[i].start, r->seg[i].end);
        if ((i + 1) % 6 == 0 && i + 1 < r->nseg)
            printf("|\n  ");
    }
    printf("|\n");
}

static void print_result(const SchedResult *r)
{
    int i;

    banner(algo_name(r->algo));
    printf("Tie-breaking rule : %s\n", tie_rule(r->algo));
    if (r->algo == ALGO_RR)
        printf("Time quantum      : %d\n", r->quantum);
    print_job_set(&r->jobs);
    print_timeline(r);
    printf("\n  %-5s %-8s %-6s %-5s %-11s %-11s %-8s\n", "Job", "Arrival", "Burst",
           "Start", "Completion", "Turnaround", "Waiting");
    for (i = 0; i < r->jobs.count; i++) {
        const Job *j = &r->jobs.jobs[i];
        printf("  P%-4d %-8d %-6d %-5d %-11d %-11d %-8d\n", j->id, j->arrival,
               j->burst, j->start, j->completion, j->turnaround, j->waiting);
    }
    printf("\n  Average completion time : %.2f\n", r->avg_ct);
    printf("  Average turnaround time : %.2f\n", r->avg_tat);
    printf("  Average waiting time    : %.2f\n", r->avg_wt);
    printf("  Context switches        : %d\n", r->context_switches);
}

static void print_order_line(const SchedResult *r)
{
    int i;
    printf("  Order: ");
    for (i = 0; i < r->nseg; i++) {
        if (r->seg[i].job_id < 0)
            printf("IDLE ");
        else
            printf("P%d ", r->seg[i].job_id);
    }
    printf("\n");
}

static int do_compare(const JobSet *js, int quantum)
{
    static const SchedAlgo algos[3] = { ALGO_FCFS, ALGO_RR, ALGO_PRIORITY };
    SchedResult res[3];
    int i, best = 0;

    banner("Scheduling comparison (same job set)");
    print_job_set(js);
    printf("Round Robin quantum = %d\n\n", quantum);
    for (i = 0; i < 3; i++) {
        if (sched_run(js, algos[i], quantum, &res[i]) != 0)
            return 1;
        printf("%s\n", algo_name(algos[i]));
        print_order_line(&res[i]);
        if (res[i].avg_wt < res[best].avg_wt)
            best = i;
    }
    printf("\n  %-26s %-8s %-8s %-8s %-9s\n", "Policy", "Avg CT", "Avg TAT",
           "Avg WT", "Ctx-sw");
    for (i = 0; i < 3; i++)
        printf("  %-26s %-8.2f %-8.2f %-8.2f %-9d\n", algo_name(algos[i]),
               res[i].avg_ct, res[i].avg_tat, res[i].avg_wt, res[i].context_switches);
    printf("\nLowest average waiting time: %s (%.2f)\n", algo_name(algos[best]),
           res[best].avg_wt);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Command entry point                                                 */
/* ------------------------------------------------------------------ */

int cmd_sched(const char *algo, int quantum, const char *file)
{
    JobSet js;
    SchedResult r;
    SchedAlgo a;

    if (file != NULL) {
        if (jobs_load_file(&js, file) != 0)
            return 1;
        printf("Job source: file '%s'\n", file);
    } else {
        jobs_load_default(&js);
        printf("Job source: built-in repeatable test set\n");
    }
    if (jobs_validate(&js) != 0)
        return 1;

    if (strcmp(algo, "compare") == 0)
        return do_compare(&js, quantum);
    if (strcmp(algo, "fcfs") == 0)
        a = ALGO_FCFS;
    else if (strcmp(algo, "rr") == 0)
        a = ALGO_RR;
    else if (strcmp(algo, "prio") == 0 || strcmp(algo, "priority") == 0)
        a = ALGO_PRIORITY;
    else {
        fprintf(stderr, "error: unknown scheduler '%s' (use fcfs|rr|prio|compare)\n", algo);
        return 2;
    }
    if (sched_run(&js, a, quantum, &r) != 0)
        return 1;
    print_result(&r);
    return 0;
}
