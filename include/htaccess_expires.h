/**
 * htaccess_expires.h - Expires duration parsing for OLS .htaccess module
 *
 * Parses Apache-style expiration duration strings of the form
 * "access plus N seconds/minutes/hours/days/months/years" into
 * a total number of seconds. Supports combined formats such as
 * "access plus 1 month 2 days".
 *
 * Validates: Requirements 10.4
 */
#ifndef HTACCESS_EXPIRES_H
#define HTACCESS_EXPIRES_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse an Apache-style expiration duration string.
 *
 * Accepted format:
 *   "access plus <N unit> [<N unit> ...]"
 *
 * Where each <N unit> is an integer followed by one of:
 *   second(s), minute(s), hour(s), day(s), month(s), year(s)
 *
 * Keywords ("access", "plus", unit names) are case-insensitive.
 * Both singular and plural unit forms are accepted.
 *
 * Unit conversions:
 *   second  = 1
 *   minute  = 60
 *   hour    = 3600
 *   day     = 86400
 *   month   = 2592000  (30 days)
 *   year    = 31536000 (365 days)
 *
 * Multiple <N unit> pairs are summed, e.g.:
 *   "access plus 1 month 2 days" → 2592000 + 172800 = 2764800
 *
 * @param duration_str  Input string (must not be NULL).
 * @return Total seconds on success, -1 on error (invalid format).
 */
long parse_expires_duration(const char *duration_str);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* HTACCESS_EXPIRES_H */
