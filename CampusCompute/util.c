/* util.c - small helpers: strict integer parsing, timestamps, banners */
#include "campus.h"

/* Parse a base-10 integer. Rejects empty strings, trailing junk and
 * out-of-range values. Returns 0 on success, -1 on failure. */
int parse_int_strict(const char *s, long lo, long hi, long *out)
{
    char *end = NULL;
    long v;

    if (s == NULL || *s == '\0')
        return -1;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0' || v < lo || v > hi)
        return -1;
    *out = v;
    return 0;
}

/* HH:MM:SS.mmm wall-clock timestamp (used in log lines). */
void timestamp(char *buf, size_t n)
{
    struct timespec ts;
    struct tm tmv;
    char t[16];

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tmv);
    strftime(t, sizeof t, "%H:%M:%S", &tmv);
    snprintf(buf, n, "%s.%03ld", t, ts.tv_nsec / 1000000);
}

void banner(const char *title)
{
    printf("\n==================== %s ====================\n", title);
}
