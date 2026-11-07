/*
 * campus.h - shared declarations for CampusCompute
 * Operating Systems (UE25CS242A) - Level 3 Mini-Project
 */
#ifndef CAMPUS_H
#define CAMPUS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* ---------- Limits used for input validation ---------- */
#define MAX_JOBS         32
#define MAX_SEGMENTS     4096
#define MAX_BURST        100
#define MAX_TIME         1000
#define MAX_PRIORITY     99
#define DEFAULT_QUANTUM  4

/* ---------- FR1: job model ---------- */
typedef enum { JOB_NEW, JOB_READY, JOB_RUNNING, JOB_DONE } JobStatus;
typedef enum { ALGO_FCFS, ALGO_RR, ALGO_PRIORITY } SchedAlgo;

typedef struct {
    int id;
    int arrival;
    int burst;
    int priority;       /* lower number = higher priority */
    int remaining;
    int start;          /* first time on CPU (-1 = never) */
    int completion;
    int turnaround;     /* completion - arrival */
    int waiting;        /* turnaround - burst   */
    JobStatus status;
} Job;

typedef struct {
    int count;
    Job jobs[MAX_JOBS];
} JobSet;

/* One slice of the Gantt chart. job_id == -1 means CPU idle. */
typedef struct {
    int job_id;
    int start;
    int end;
} Segment;

typedef struct {
    SchedAlgo algo;
    int quantum;
    JobSet jobs;
    Segment seg[MAX_SEGMENTS];
    int nseg;
    int overflow;
    int makespan;
    int context_switches;
    double avg_ct, avg_tat, avg_wt;
} SchedResult;

/* ---------- util.c ---------- */
int  parse_int_strict(const char *s, long lo, long hi, long *out);
void timestamp(char *buf, size_t n);
void banner(const char *title);

/* ---------- scheduler.c (FR1, FR2) ---------- */
int  jobs_load_default(JobSet *js);
int  jobs_load_file(JobSet *js, const char *path);
int  jobs_validate(const JobSet *js);
int  sched_run(const JobSet *in, SchedAlgo algo, int quantum, SchedResult *r);
int  cmd_sched(const char *algo, int quantum, const char *file);

/* ---------- logger.c (FR3) ---------- */
int  cmd_logger(int items_per_producer, int bufsize, const char *logpath);

/* ---------- ipc.c (FR4) ---------- */
int  cmd_ipc(void);

/* ---------- banker.c (FR5) ---------- */
int  cmd_banker(const char *which);

/* ---------- worker.c (FR6) ---------- */
int  cmd_worker(const char *mode);

/* ---------- limits.c (FR7) ---------- */
int  cmd_limit(const char *which);

#endif
