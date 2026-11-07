/*
 * banker.c - FR5: Banker's safety algorithm
 *
 *   Need = Maximum - Allocation
 *   Work = Available; Finish[i] = false
 *   repeat: find unfinished i with Need[i] <= Work
 *           Work += Allocation[i]; Finish[i] = true; append i to sequence
 *   safe  <=>  every process can finish
 *
 * Two built-in datasets (5 processes, 3 resource types A,B,C):
 *   safe   : classic textbook state, safe sequence exists
 *   unsafe : same Allocation/Maximum, Available reduced to (0 1 1);
 *            P3 and P1 can finish, then the rest are stuck -> unsafe
 */
#include "campus.h"

#define MAXP 10
#define MAXR 10

typedef struct {
    int n, m;
    int avail[MAXR];
    int alloc[MAXP][MAXR];
    int max[MAXP][MAXR];
} BankerState;

static void print_vec(const int *v, int m)
{
    int j;
    printf("(");
    for (j = 0; j < m; j++)
        printf("%s%d", j ? " " : "", v[j]);
    printf(")");
}

static void print_state(const BankerState *s, int need[][MAXR])
{
    int i, j;

    printf("  Available = ");
    print_vec(s->avail, s->m);
    printf("\n\n  %-4s | %-10s | %-10s | %-10s\n", "Proc", "Allocation", "Maximum", "Need");
    for (i = 0; i < s->n; i++) {
        printf("  P%-3d | ", i);
        for (j = 0; j < s->m; j++) printf("%-3d", s->alloc[i][j]);
        printf("| ");
        for (j = 0; j < s->m; j++) printf("%-3d", s->max[i][j]);
        printf("| ");
        for (j = 0; j < s->m; j++) printf("%-3d", need[i][j]);
        printf("\n");
    }
    printf("\n");
}

/* Returns 1 if safe (seq[] filled with n entries), 0 if unsafe. */
static int is_safe(const BankerState *s, int need[][MAXR], int *seq)
{
    int work[MAXR], finish[MAXP] = {0};
    int count = 0, progress = 1, i, j;

    for (j = 0; j < s->m; j++)
        work[j] = s->avail[j];

    while (count < s->n && progress) {
        progress = 0;
        for (i = 0; i < s->n; i++) {
            int ok = 1;
            if (finish[i])
                continue;
            for (j = 0; j < s->m; j++)
                if (need[i][j] > work[j]) { ok = 0; break; }
            if (!ok)
                continue;
            printf("  P%d: Need ", i);
            print_vec(need[i], s->m);
            printf(" <= Work ");
            print_vec(work, s->m);
            for (j = 0; j < s->m; j++)
                work[j] += s->alloc[i][j];
            printf("  -> finishes, Work becomes ");
            print_vec(work, s->m);
            printf("\n");
            finish[i] = 1;
            seq[count++] = i;
            progress = 1;
        }
    }
    if (count < s->n) {
        printf("  No remaining process can proceed with Work ");
        print_vec(work, s->m);
        printf("\n  Blocked processes:");
        for (i = 0; i < s->n; i++)
            if (!finish[i]) {
                printf(" P%d need ", i);
                print_vec(need[i], s->m);
            }
        printf("\n");
        return 0;
    }
    return 1;
}

static void load_dataset(BankerState *s, int safe)
{
    static const int alloc[5][3] = {{0,1,0},{2,0,0},{3,0,2},{2,1,1},{0,0,2}};
    static const int max[5][3]   = {{7,5,3},{3,2,2},{9,0,2},{2,2,2},{4,3,3}};
    int i, j;

    memset(s, 0, sizeof *s);
    s->n = 5;
    s->m = 3;
    for (i = 0; i < 5; i++)
        for (j = 0; j < 3; j++) {
            s->alloc[i][j] = alloc[i][j];
            s->max[i][j] = max[i][j];
        }
    if (safe) { s->avail[0] = 3; s->avail[1] = 3; s->avail[2] = 2; }
    else      { s->avail[0] = 0; s->avail[1] = 1; s->avail[2] = 1; }
}

/* Returns 0 when the outcome matches what the dataset was designed to show. */
int cmd_banker(const char *which)
{
    BankerState s;
    int need[MAXP][MAXR], seq[MAXP];
    int want_safe, i, j, safe;

    if (strcmp(which, "safe") == 0)
        want_safe = 1;
    else if (strcmp(which, "unsafe") == 0)
        want_safe = 0;
    else {
        fprintf(stderr, "error: unknown banker case '%s' (use safe|unsafe)\n", which);
        return 2;
    }
    load_dataset(&s, want_safe);
    for (i = 0; i < s.n; i++)
        for (j = 0; j < s.m; j++) {
            need[i][j] = s.max[i][j] - s.alloc[i][j];
            if (need[i][j] < 0) {
                fprintf(stderr, "error: Maximum < Allocation for P%d\n", i);
                return 1;
            }
        }

    banner(want_safe ? "Banker's algorithm: SAFE state test"
                     : "Banker's algorithm: UNSAFE state test");
    print_state(&s, need);
    printf("Safety check (Need = Maximum - Allocation):\n");
    safe = is_safe(&s, need, seq);
    if (safe) {
        printf("\nRESULT: SAFE state. Safe sequence: ");
        for (i = 0; i < s.n; i++)
            printf("P%d%s", seq[i], i + 1 < s.n ? " -> " : "\n");
    } else {
        printf("\nRESULT: UNSAFE state - no safe sequence exists (possible deadlock)\n");
    }
    return (safe == want_safe) ? 0 : 1;
}
