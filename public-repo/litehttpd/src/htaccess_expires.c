/**
 * htaccess_expires.c - Expires duration parsing implementation
 *
 * Validates: Requirements 10.4
 */
#include "htaccess_expires.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Time unit multipliers */
#define SECS_PER_SECOND  1L
#define SECS_PER_MINUTE  60L
#define SECS_PER_HOUR    3600L
#define SECS_PER_DAY     86400L
#define SECS_PER_MONTH   2592000L   /* 30 days */
#define SECS_PER_YEAR    31536000L  /* 365 days */

/**
 * Skip whitespace characters, returning a pointer to the first
 * non-whitespace character (or the terminating NUL).
 */
static const char *skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p))
        p++;
    return p;
}

/**
 * Try to match a keyword (case-insensitive) at position `p`.
 * On success, returns a pointer past the keyword; on failure returns NULL.
 * The character after the keyword must be whitespace or NUL.
 */
static const char *match_keyword(const char *p, const char *keyword)
{
    size_t len = strlen(keyword);

    if (strncasecmp(p, keyword, len) != 0)
        return NULL;

    /* Ensure the keyword is followed by whitespace or end-of-string */
    if (p[len] != '\0' && !isspace((unsigned char)p[len]))
        return NULL;

    return p + len;
}

/**
 * Map a unit name (case-insensitive, singular or plural) to its
 * multiplier in seconds.  Returns the multiplier, or -1 if the
 * unit name is not recognised.
 *
 * `unit` points to the start of the unit word; `unit_len` is its length.
 */
static long unit_to_seconds(const char *unit, size_t unit_len)
{
    /* Table of recognised units (singular form, length, multiplier) */
    static const struct {
        const char *name;
        size_t      base_len;   /* length of singular form */
        long        multiplier;
    } units[] = {
        { "second", 6, SECS_PER_SECOND },
        { "minute", 6, SECS_PER_MINUTE },
        { "hour",   4, SECS_PER_HOUR   },
        { "day",    3, SECS_PER_DAY    },
        { "month",  5, SECS_PER_MONTH  },
        { "year",   4, SECS_PER_YEAR   },
    };
    size_t i;

    for (i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
        /* Accept exact singular match */
        if (unit_len == units[i].base_len &&
            strncasecmp(unit, units[i].name, unit_len) == 0)
            return units[i].multiplier;

        /* Accept plural form (singular + 's') */
        if (unit_len == units[i].base_len + 1 &&
            strncasecmp(unit, units[i].name, units[i].base_len) == 0 &&
            (unit[unit_len - 1] == 's' || unit[unit_len - 1] == 'S'))
            return units[i].multiplier;
    }

    return -1;
}

/* ------------------------------------------------------------------ */

long parse_expires_duration(const char *duration_str)
{
    const char *p;
    long total = 0;
    int  found_pair = 0;

    if (!duration_str)
        return -1;

    p = skip_ws(duration_str);

    /* Compact format: "A<seconds>" or "M<seconds>"
     * A = access time, M = modification time (treated as access time
     * since LSIAPI cannot retrieve file mtime). */
    if ((*p == 'A' || *p == 'a' || *p == 'M' || *p == 'm') &&
        isdigit((unsigned char)p[1])) {
        char *ep;
        long secs = strtol(p + 1, &ep, 10);
        if (ep > p + 1 && (*ep == '\0' || isspace((unsigned char)*ep)) &&
            secs >= 0)
            return secs;
    }

    /* Verbose format: "access plus N unit" or "modification plus N unit"
     * Note: OLS cannot retrieve file mtime, so "modification" is treated
     * the same as "access" (from current time). This matches CyberPanel
     * and avoids rejecting valid Apache .htaccess files. */
    const char *pp = match_keyword(p, "access");
    if (!pp)
        pp = match_keyword(p, "modification");
    if (!pp)
        return -1;
    p = pp;

    p = skip_ws(p);

    /* Expect "plus" keyword */
    p = match_keyword(p, "plus");
    if (!p)
        return -1;

    p = skip_ws(p);

    /* Parse one or more "<N> <unit>" pairs */
    while (*p) {
        char *ep;
        long  n;
        const char *unit_start;
        size_t unit_len;
        long   multiplier;

        /* Parse the integer value */
        if (!isdigit((unsigned char)*p))
            return -1;

        n = strtol(p, &ep, 10);
        if (ep == p || n < 0)
            return -1;

        p = skip_ws(ep);

        /* Parse the unit name */
        if (!*p || !isalpha((unsigned char)*p))
            return -1;

        unit_start = p;
        while (*p && isalpha((unsigned char)*p))
            p++;
        unit_len = (size_t)(p - unit_start);

        multiplier = unit_to_seconds(unit_start, unit_len);
        if (multiplier < 0)
            return -1;

        total += n * multiplier;
        found_pair = 1;

        p = skip_ws(p);
    }

    /* Must have at least one <N unit> pair */
    if (!found_pair)
        return -1;

    return total;
}
