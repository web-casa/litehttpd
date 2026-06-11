/**
 * htaccess_exec_auth.c - AuthType Basic executor
 *
 * Collects auth config from directive list, validates Authorization header
 * against htpasswd file entries. Supports crypt hash format.
 *
 * Validates: Requirements 10.1-10.9
 */
#define _GNU_SOURCE
#include "htaccess_exec_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <crypt.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>

/** Maximum base64-encoded credential length we accept (user:pass). */
#define MAX_AUTH_B64_LEN 1024

/* Base64 decode table */
static const unsigned char b64_table[256] = {
    ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
    ['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
    ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
    ['Y']=24,['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,
    ['g']=32,['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
    ['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,
    ['w']=48,['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,
    ['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,['+']=62,['/']=63,
};

/**
 * Simple base64 decode. Returns decoded length, or -1 on error.
 */
/* Validate that a character is valid base64 */
static int is_b64_char(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
}

static int base64_decode(const char *in, size_t in_len,
                         unsigned char *out, size_t out_cap)
{
    size_t i, j = 0;
    unsigned char buf[4];
    int pad;

    for (i = 0; i < in_len; i += 4) {
        int k;
        pad = 0;
        memset(buf, 0, sizeof(buf));
        for (k = 0; k < 4 && (i + k) < in_len; k++) {
            unsigned char c = (unsigned char)in[i + k];
            if (!is_b64_char(c))
                return -1;  /* reject invalid characters */
            if (c == '=') { pad++; buf[k] = 0; }
            else buf[k] = b64_table[c];
        }
        /* Incomplete group (fewer than 4 chars without padding) */
        if (k < 4 && pad == 0)
            return -1;
        if (j + 3 > out_cap) return -1;
        out[j++] = (buf[0] << 2) | (buf[1] >> 4);
        if (pad < 2) out[j++] = (buf[1] << 4) | (buf[2] >> 2);
        if (pad < 1) out[j++] = (buf[2] << 6) | buf[3];
    }
    return (int)j;
}

int htpasswd_check(const char *hash, const char *password)
{
    if (!hash || !password)
        return -1;

    /* Use thread-safe crypt_r() with thread-local storage to avoid
     * zeroing the full struct crypt_data (~256KB on glibc) per call */
    static __thread struct crypt_data cdata;
    static __thread int cdata_ready = 0;
    if (!cdata_ready) {
        memset(&cdata, 0, sizeof(cdata));
        cdata_ready = 1;
    }
    cdata.initialized = 0;
    char *result = crypt_r(password, hash, &cdata);
    if (!result)
        return -1;

    /* Constant-time comparison to prevent timing side-channel attacks */
    size_t len = strlen(result);
    size_t hlen = strlen(hash);
    if (len != hlen)
        return 0;
    volatile unsigned char diff = 0;
    for (size_t i = 0; i < len; i++)
        diff |= (unsigned char)result[i] ^ (unsigned char)hash[i];
    return (diff == 0) ? 1 : 0;
}

/**
 * Check whether `user` appears in a space/tab-separated username list.
 * Comparison is exact and case-sensitive (matching Apache htpasswd semantics).
 */
static int username_in_list(const char *user, const char *list)
{
    if (!user || !list)
        return 0;
    size_t ulen = strlen(user);
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        size_t tlen = (size_t)(p - start);
        if (tlen == ulen && memcmp(start, user, ulen) == 0)
            return 1;
    }
    return 0;
}

/**
 * Evaluate whether `user` satisfies the "Require user" username constraints in
 * one level of the Require tree, honouring container boolean semantics:
 *   - mode 0 (OR: top level / RequireAny): satisfied if ANY user constraint at
 *     this level matches (or any nested container is satisfied).
 *   - mode 1 (AND: RequireAll): satisfied only if EVERY user constraint matches
 *     and every nested container is satisfied — so "<RequireAll> Require user
 *     alice; Require user bob </RequireAll>" requires BOTH (i.e. denies alice),
 *     instead of the previous flat-OR which would have let alice through.
 *
 * Non-user directives (ip/env/valid-user/...) are NOT evaluated here — they are
 * handled by exec_require against the request. In OR mode we therefore do NOT
 * credit a non-user branch as satisfying the username (we can't tell here if it
 * matched), keeping the decision fail-closed.
 *
 * `*seen` is set if any DIR_REQUIRE_USER constraint exists in this subtree.
 * Recurses through the two levels of nesting the parser permits.
 */
static int eval_user_tree(const htaccess_directive_t *list, int mode,
                          const char *user, int depth, int *seen)
{
    const htaccess_directive_t *dir;
    int or_match = 0;       /* OR mode: did some user branch match? */
    int and_ok = 1;         /* AND mode: have all user constraints matched? */

    for (dir = list; dir; dir = dir->next) {
        if (dir->type == DIR_REQUIRE_USER) {
            *seen = 1;
            int m = username_in_list(user, dir->value);
            if (mode == 1) { if (!m) and_ok = 0; }
            else           { if (m)  or_match = 1; }
        } else if ((dir->type == DIR_REQUIRE_ANY_OPEN ||
                    dir->type == DIR_REQUIRE_ALL_OPEN) && depth < 2) {
            int sub_mode = (dir->type == DIR_REQUIRE_ALL_OPEN) ? 1 : 0;
            int sub_seen = 0;
            int r = eval_user_tree(dir->data.require_container.children,
                                   sub_mode, user, depth + 1, &sub_seen);
            if (sub_seen) {
                *seen = 1;
                if (mode == 1) { if (!r) and_ok = 0; }
                else           { if (r)  or_match = 1; }
            }
            /* A nested container with no user constraint does not affect the
             * username decision at this level. */
        }
    }
    return (mode == 1) ? and_ok : or_match;
}

/**
 * After successful authentication, enforce any "Require user" username lists.
 * Returns 1 if access is permitted. A bare top-level "Require valid-user"
 * widens acceptance to any valid user (require_any_valid_user); otherwise the
 * username must satisfy the Require tree's user constraints (see eval_user_tree).
 */
static int user_satisfies_require(const char *user,
                                  const htaccess_directive_t *directives,
                                  int require_any_valid_user)
{
    if (require_any_valid_user)
        return 1;

    int has_user_constraint = 0;
    /* Top-level Require list is an implicit OR (mode 0). */
    int ok = eval_user_tree(directives, 0, user, 0, &has_user_constraint);

    /* No "Require user" constraint at all → any valid user is fine. */
    if (!has_user_constraint)
        return 1;
    return ok;
}

/**
 * Open an AuthUserFile safely. Performs ALL security checks in one place:
 *   - reject relative paths and ".." traversal,
 *   - confine the resolved path to the document-root subtree (prevents using
 *     the Basic-Auth endpoint as an oracle against /etc/shadow or other
 *     tenants' files); fail closed if the docroot can't be determined,
 *   - open the canonical path with O_NOFOLLOW + fstat() to close the
 *     realpath()->open() TOCTOU window (a final-component symlink swap) and
 *     ensure a regular file.
 * Returns an open FILE* (caller fclose) or NULL on any rejection/error.
 */
static FILE *open_confined_htpasswd(lsi_session_t *session, const char *path)
{
    if (!path || !path[0] || path[0] != '/' || strstr(path, ".."))
        return NULL;

    int dr_len = 0;
    const char *doc_root = lsi_session_get_doc_root(session, &dr_len);
    char real_auth[PATH_MAX];
    char real_root[PATH_MAX];
    if (!doc_root || dr_len <= 0 ||
        !realpath(path, real_auth) || !realpath(doc_root, real_root))
        return NULL;

    size_t rr_len = strlen(real_root);
    while (rr_len > 1 && real_root[rr_len - 1] == '/')
        real_root[--rr_len] = '\0';
    if (strncmp(real_auth, real_root, rr_len) != 0 ||
        (real_auth[rr_len] != '\0' && real_auth[rr_len] != '/'))
        return NULL;

    int fd = open(real_auth, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return NULL;

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return NULL;
    }

    /* TOCTOU defence: O_NOFOLLOW only guards the final component. An attacker
     * who controls the docroot could swap an *intermediate* directory for a
     * symlink between realpath() and open(), making the opened file escape the
     * docroot. Re-resolve the path actually opened (via /proc/self/fd) and
     * re-confirm it is still beneath the document root. */
    {
        char fdlink[64];
        char opened[PATH_MAX];
        snprintf(fdlink, sizeof(fdlink), "/proc/self/fd/%d", fd);
        ssize_t rl = readlink(fdlink, opened, sizeof(opened) - 1);
        if (rl < 0) {
            close(fd);
            return NULL;
        }
        opened[rl] = '\0';
        if (strncmp(opened, real_root, rr_len) != 0 ||
            (opened[rr_len] != '\0' && opened[rr_len] != '/')) {
            lsi_log(session, LSI_LOG_ERROR,
                    "[htaccess] AuthUserFile resolved outside document root "
                    "after open ('%s') — rejected", opened);
            close(fd);
            return NULL;
        }
    }

    if (st.st_mode & 0004)
        lsi_log(session, LSI_LOG_WARN,
                "[htaccess] AuthUserFile '%s' is world-readable (mode %o), "
                "consider restricting permissions",
                real_auth, (unsigned)(st.st_mode & 0777));

    FILE *fp = fdopen(fd, "r");
    if (!fp) {
        close(fd);
        return NULL;
    }
    return fp;
}

/**
 * Parse "Basic <base64>" from Authorization header.
 * Returns 1 on success, fills user/pass (caller frees).
 */
static int parse_basic_auth(const char *auth_header, int auth_len,
                            char **out_user, char **out_pass)
{
    if (!auth_header || auth_len < 7)
        return 0;

    /* Must start with "Basic " */
    if (strncasecmp(auth_header, "Basic ", 6) != 0)
        return 0;

    const char *b64 = auth_header + 6;
    size_t b64_len = (size_t)(auth_len - 6);

    /* Reject excessively long credentials */
    if (b64_len > MAX_AUTH_B64_LEN)
        return 0;

    /* Dynamically allocate decode buffer based on input size */
    size_t decode_cap = (b64_len / 4) * 3 + 4;
    unsigned char *decoded = malloc(decode_cap);
    if (!decoded)
        return 0;

    int dec_len = base64_decode(b64, b64_len, decoded, decode_cap - 1);
    if (dec_len <= 0) {
        free(decoded);
        return 0;
    }
    decoded[dec_len] = '\0';

    /* Split on ':' */
    char *colon = strchr((char *)decoded, ':');
    if (!colon) {
        free(decoded);
        return 0;
    }

    *colon = '\0';
    *out_user = strdup((char *)decoded);
    *out_pass = strdup(colon + 1);
    free(decoded);
    if (!*out_user || !*out_pass) {
        free(*out_user);
        free(*out_pass);
        *out_user = NULL;
        *out_pass = NULL;
        return 0;
    }
    return 1;
}

int exec_auth_basic(lsi_session_t *session,
                    const htaccess_directive_t *directives)
{
    if (!session || !directives)
        return LSI_OK;

    /* Collect auth config from directive list */
    const char *auth_type = NULL;
    const char *auth_name = NULL;
    const char *auth_user_file = NULL;
    int require_valid_user = 0;       /* auth required (valid-user OR user list) */
    int require_any_valid_user = 0;   /* a bare "Require valid-user" is present */

    const htaccess_directive_t *dir;
    for (dir = directives; dir; dir = dir->next) {
        switch (dir->type) {
        case DIR_AUTH_TYPE:
            auth_type = dir->value;
            break;
        case DIR_AUTH_NAME:
            auth_name = dir->value;
            break;
        case DIR_AUTH_USER_FILE:
            auth_user_file = dir->value;
            break;
        case DIR_REQUIRE_VALID_USER:
            require_valid_user = 1;
            require_any_valid_user = 1;
            break;
        case DIR_REQUIRE_USER:
            /* "Require user u1 u2" — authentication required; the username
             * list is enforced post-auth by user_satisfies_require(). */
            require_valid_user = 1;
            break;
        case DIR_REQUIRE_ANY_OPEN:
        case DIR_REQUIRE_ALL_OPEN: {
            /* Recursively scan inside RequireAny/RequireAll for valid-user/user.
             * NOTE: a "valid-user" found INSIDE a container does NOT set
             * require_any_valid_user. Only a top-level bare "Require valid-user"
             * widens acceptance to any authenticated user (top-level Require is
             * an implicit OR). Inside a RequireAll, "valid-user" must coexist
             * with — not override — any "Require user" username constraint, so
             * widening there would be a fail-open. Both only require that auth
             * happens (require_valid_user); the username list is then enforced
             * by user_satisfies_require(). */
            const htaccess_directive_t *child;
            for (child = dir->data.require_container.children; child; child = child->next) {
                if (child->type == DIR_REQUIRE_VALID_USER ||
                    child->type == DIR_REQUIRE_USER)
                    require_valid_user = 1;
                /* Check nested containers too */
                if ((child->type == DIR_REQUIRE_ANY_OPEN ||
                     child->type == DIR_REQUIRE_ALL_OPEN)) {
                    const htaccess_directive_t *gc;
                    for (gc = child->data.require_container.children; gc; gc = gc->next) {
                        if (gc->type == DIR_REQUIRE_VALID_USER ||
                            gc->type == DIR_REQUIRE_USER)
                            require_valid_user = 1;
                    }
                }
            }
            break;
        }
        case DIR_IF:
        case DIR_ELSEIF:
        case DIR_ELSE:
            /* Do NOT scan auth directives inside conditional blocks here.
             * If/ElseIf/Else auth is evaluated during condition chain
             * evaluation (eval_if_chain → exec_if_child_request), not
             * in the unconditional pre-scan. Scanning here would trigger
             * 401 for unmatched branches, violating Apache semantics. */
            break;
        default:
            break;
        }
    }

    /* If no AuthType Basic + Require valid-user, nothing to do */
    if (!auth_type || strcasecmp(auth_type, "Basic") != 0)
        return LSI_OK;
    if (!require_valid_user)
        return LSI_OK;

    /* KNOWN LIMITATION (intentional, fail-closed): access control is split
     * across two phases — exec_require() evaluates ip/env/valid-user against
     * the request, while this function enforces credentials and "Require user"
     * username lists. Neither phase sees the other's per-branch result. So a
     * mixed OR such as
     *     <RequireAny> Require ip 10.0.0.0/8 ; Require user alice </RequireAny>
     * is evaluated more strictly than Apache: a user coming from 10.0.0.0/8 who
     * is not "alice" is still challenged/denied here. This OVER-DENIES (safe,
     * fail-closed) rather than risking a fail-open. Granting exactly like Apache
     * would require evaluating the whole Require boolean tree together with the
     * authenticated identity in a single pass (a larger refactor of the
     * exec_require/exec_auth split). */

    /* AuthUserFile is required */
    if (!auth_user_file) {
        lsi_log(session, LSI_LOG_ERROR,
                "[htaccess] AuthUserFile not specified");
        lsi_session_set_status(session, 500);
        return LSI_ERROR;
    }

    /* Get Authorization header */
    int auth_len = 0;
    const char *auth_header = lsi_session_get_auth_header(session, &auth_len);

    char *user = NULL;
    char *pass = NULL;
    if (!auth_header || auth_len <= 0 ||
        !parse_basic_auth(auth_header, auth_len, &user, &pass)) {
        /* No credentials — send 401 */
        if (auth_name)
            lsi_session_set_www_authenticate(session, auth_name,
                                             (int)strlen(auth_name));
        lsi_session_set_status(session, 401);
        return LSI_ERROR;
    }

    /* Open the htpasswd file with all path/confinement/symlink checks applied
     * atomically (see open_confined_htpasswd). Any rejection → 500 fail-closed. */
    FILE *fp = open_confined_htpasswd(session, auth_user_file);
    if (!fp) {
        lsi_log(session, LSI_LOG_ERROR,
                "[htaccess] AuthUserFile rejected or unreadable: %s",
                auth_user_file);
        free(user);
        free(pass);
        lsi_session_set_status(session, 500);
        return LSI_ERROR;
    }

    /* Search for matching user */
    char line[512];
    int authenticated = 0;
    while (fgets(line, sizeof(line), fp)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';

        if (strcmp(line, user) != 0) continue;

        /* Found user — check password */
        const char *hash = colon + 1;
        if (htpasswd_check(hash, pass) == 1) {
            authenticated = 1;
            break;
        }
    }
    fclose(fp);

    if (!authenticated) {
        free(user);
        free(pass);
        if (auth_name)
            lsi_session_set_www_authenticate(session, auth_name,
                                             (int)strlen(auth_name));
        lsi_session_set_status(session, 401);
        return LSI_ERROR;
    }

    /* Credentials valid — now enforce any "Require user" username list.
     * A bare "Require valid-user" widens acceptance to any valid user. */
    if (!user_satisfies_require(user, directives, require_any_valid_user)) {
        lsi_log(session, LSI_LOG_DEBUG,
                "[htaccess] user '%s' authenticated but not in Require user list",
                user);
        free(user);
        free(pass);
        lsi_session_set_status(session, 403);
        return LSI_ERROR;
    }

    free(user);
    free(pass);
    return LSI_OK;
}

int check_auth_credentials(lsi_session_t *session,
                           const htaccess_directive_t *directives)
{
    if (!session || !directives) return 0;

    /* Find auth config */
    const char *auth_type = NULL;
    const char *auth_user_file = NULL;
    const htaccess_directive_t *dir;
    for (dir = directives; dir; dir = dir->next) {
        if (dir->type == DIR_AUTH_TYPE) auth_type = dir->value;
        else if (dir->type == DIR_AUTH_USER_FILE) auth_user_file = dir->value;
    }

    if (!auth_type || strcasecmp(auth_type, "Basic") != 0)
        return 0;
    if (!auth_user_file || !auth_user_file[0] || auth_user_file[0] != '/')
        return 0;

    /* Get Authorization header */
    int auth_len = 0;
    const char *auth_header = lsi_session_get_auth_header(session, &auth_len);
    char *user = NULL;
    char *pass = NULL;
    if (!auth_header || auth_len <= 0 ||
        !parse_basic_auth(auth_header, auth_len, &user, &pass))
        return 0;

    /* Apply the SAME confinement/symlink/TOCTOU checks as exec_auth_basic.
     * This pre-validation path must not become a way to read or time-probe
     * arbitrary server-readable files outside the document root. */
    FILE *fp = open_confined_htpasswd(session, auth_user_file);
    if (!fp) { free(user); free(pass); return 0; }

    char line[512];
    int valid = 0;
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        if (strcmp(line, user) == 0 && htpasswd_check(colon + 1, pass) == 1) {
            valid = 1;
            break;
        }
    }
    fclose(fp);
    free(user);
    free(pass);
    return valid;
}
