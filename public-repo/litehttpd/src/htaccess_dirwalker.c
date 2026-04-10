/**
 * htaccess_dirwalker.c - Directory hierarchy traversal implementation
 *
 * Walks from document root to target directory, collecting .htaccess
 * directives at each level via the cache, then merges them with
 * child-overrides-parent semantics.
 *
 * Validates: Requirements 13.1, 13.2, 13.3
 */
#include "htaccess_dirwalker.h"
#include "htaccess_cache.h"
#include "htaccess_parser.h"
#include "htaccess_expr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* Maximum directory depth we support */
#define MAX_DIR_DEPTH 64

/* Maximum path length */
#define MAX_PATH_LEN 4096

/* Maximum .htaccess file size — adaptive (Apache behavior).
 * Starts at 1MB, shrinks if slow parses detected. Floor: 200KB. */
#define MAX_HTACCESS_FILE_SIZE_INIT (1024 * 1024)
#define MAX_HTACCESS_FILE_SIZE_FLOOR (200 * 1024)
static __thread int htaccess_size_limit = MAX_HTACCESS_FILE_SIZE_INIT;

/* ------------------------------------------------------------------ */
/* Negative stat cache: avoid repeated stat() on dirs without .htaccess */
/* ------------------------------------------------------------------ */
#define NEG_STAT_SLOTS 16
#define NEG_STAT_TTL   256  /* flush every N dirwalk calls */

static __thread struct {
    unsigned long hash;
    char         *path;   /* owned copy for collision check */
    int           valid;
} neg_stat_cache[NEG_STAT_SLOTS];

static __thread unsigned int neg_stat_generation = 0;

static unsigned long neg_djb2(const char *str)
{
    unsigned long h = 5381;
    while (*str)
        h = ((h << 5) + h) + (unsigned char)*str++;
    return h;
}

static void neg_stat_flush(void)
{
    for (int i = 0; i < NEG_STAT_SLOTS; i++) {
        if (neg_stat_cache[i].valid) {
            free(neg_stat_cache[i].path);
            neg_stat_cache[i].path = NULL;
            neg_stat_cache[i].valid = 0;
        }
    }
}

static int neg_stat_check(const char *path)
{
    unsigned long h = neg_djb2(path);
    for (int i = 0; i < NEG_STAT_SLOTS; i++) {
        if (neg_stat_cache[i].valid && neg_stat_cache[i].hash == h
            && strcmp(neg_stat_cache[i].path, path) == 0)
            return 1;
    }
    return 0;
}

static void neg_stat_add(const char *path)
{
    unsigned long h = neg_djb2(path);
    /* Find empty slot */
    for (int i = 0; i < NEG_STAT_SLOTS; i++) {
        if (!neg_stat_cache[i].valid) {
            neg_stat_cache[i].hash = h;
            neg_stat_cache[i].path = strdup(path);
            if (!neg_stat_cache[i].path) return;
            neg_stat_cache[i].valid = 1;
            return;
        }
    }
    /* Full — evict slot 0, shift down */
    free(neg_stat_cache[0].path);
    for (int i = 0; i < NEG_STAT_SLOTS - 1; i++)
        neg_stat_cache[i] = neg_stat_cache[i + 1];
    neg_stat_cache[NEG_STAT_SLOTS - 1].hash = h;
    neg_stat_cache[NEG_STAT_SLOTS - 1].path = strdup(path);
    if (!neg_stat_cache[NEG_STAT_SLOTS - 1].path) {
        neg_stat_cache[NEG_STAT_SLOTS - 1].valid = 0;
        return;
    }
    neg_stat_cache[NEG_STAT_SLOTS - 1].valid = 1;
}

static void neg_stat_remove(const char *path)
{
    unsigned long h = neg_djb2(path);
    for (int i = 0; i < NEG_STAT_SLOTS; i++) {
        if (neg_stat_cache[i].valid && neg_stat_cache[i].hash == h
            && strcmp(neg_stat_cache[i].path, path) == 0) {
            free(neg_stat_cache[i].path);
            neg_stat_cache[i].path = NULL;
            neg_stat_cache[i].valid = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Internal: deep-copy a children linked list (used by copy_directive) */
/* ------------------------------------------------------------------ */
static htaccess_directive_t *copy_directive(const htaccess_directive_t *src);
static htaccess_directive_t *copy_directive_list(const htaccess_directive_t *src);

static htaccess_directive_t *copy_children(const htaccess_directive_t *src)
{
    if (!src)
        return NULL;
    htaccess_directive_t *head = NULL;
    htaccess_directive_t *tail = NULL;
    for (const htaccess_directive_t *c = src; c; c = c->next) {
        htaccess_directive_t *cc = copy_directive(c);
        if (!cc) {
            htaccess_directives_free(head);
            return NULL;
        }
        if (!head)
            head = cc;
        else
            tail->next = cc;
        tail = cc;
    }
    return head;
}

/* ------------------------------------------------------------------ */
/* Internal: deep-copy a single directive node                         */
/* ------------------------------------------------------------------ */
static htaccess_directive_t *copy_directive(const htaccess_directive_t *src)
{
    htaccess_directive_t *d = calloc(1, sizeof(htaccess_directive_t));
    if (!d)
        return NULL;

    d->type = src->type;
    d->line_number = src->line_number;
    d->next = NULL;

    if (src->name) {
        d->name = strdup(src->name);
        if (!d->name) goto fail;
    }
    if (src->value) {
        d->value = strdup(src->value);
        if (!d->value) goto fail;
    }
    /* env_condition is a top-level heap field (Header ... env=VAR) */
    if (src->env_condition) {
        d->env_condition = strdup(src->env_condition);
        if (!d->env_condition) goto fail;
    }

    /* Shallow copy the union first, then deep-copy heap fields */
    d->data = src->data;

    /* Deep-copy type-specific heap fields */
    switch (src->type) {
    case DIR_REDIRECT:
    case DIR_REDIRECT_MATCH:
        if (src->data.redirect.pattern) {
            d->data.redirect.pattern = strdup(src->data.redirect.pattern);
            if (!d->data.redirect.pattern) goto fail;
        }
        break;
    case DIR_FILES_MATCH:
        d->data.files_match.pattern = NULL;
        d->data.files_match.children = NULL;
        if (src->data.files_match.pattern) {
            d->data.files_match.pattern = strdup(src->data.files_match.pattern);
            if (!d->data.files_match.pattern) goto fail;
        }
        if (src->data.files_match.children) {
            d->data.files_match.children = copy_children(src->data.files_match.children);
            if (!d->data.files_match.children) goto fail;
        }
        break;
    case DIR_FILES:
        if (src->data.files.children) {
            d->data.files.children = copy_children(src->data.files.children);
            if (!d->data.files.children) goto fail;
        }
        break;
    case DIR_IFMODULE:
        if (src->data.ifmodule.children) {
            d->data.ifmodule.children = copy_children(src->data.ifmodule.children);
            if (!d->data.ifmodule.children) goto fail;
        }
        break;
    case DIR_REQUIRE_ANY_OPEN:
    case DIR_REQUIRE_ALL_OPEN:
        if (src->data.require_container.children) {
            d->data.require_container.children = copy_children(src->data.require_container.children);
            if (!d->data.require_container.children) goto fail;
        }
        break;
    case DIR_LIMIT:
    case DIR_LIMIT_EXCEPT:
        d->data.limit.methods = NULL;
        d->data.limit.children = NULL;
        if (src->data.limit.methods) {
            d->data.limit.methods = strdup(src->data.limit.methods);
            if (!d->data.limit.methods) goto fail;
        }
        if (src->data.limit.children) {
            d->data.limit.children = copy_children(src->data.limit.children);
            if (!d->data.limit.children) goto fail;
        }
        break;
    case DIR_SETENVIF:
    case DIR_SETENVIF_NOCASE:
    case DIR_BROWSER_MATCH:
        d->data.envif.attribute = NULL;
        d->data.envif.pattern = NULL;
        if (src->data.envif.attribute) {
            d->data.envif.attribute = strdup(src->data.envif.attribute);
            if (!d->data.envif.attribute) goto fail;
        }
        if (src->data.envif.pattern) {
            d->data.envif.pattern = strdup(src->data.envif.pattern);
            if (!d->data.envif.pattern) goto fail;
        }
        break;
    case DIR_HEADER_EDIT:
    case DIR_HEADER_EDIT_STAR:
    case DIR_HEADER_ALWAYS_EDIT:
    case DIR_HEADER_ALWAYS_EDIT_STAR:
        if (src->data.header_ext.edit_pattern) {
            d->data.header_ext.edit_pattern = strdup(src->data.header_ext.edit_pattern);
            if (!d->data.header_ext.edit_pattern) goto fail;
        }
        break;
    case DIR_REWRITE_COND:
        /* Zero owned pointers first to prevent double-free on partial failure
         * (d->data was shallow-copied from src, borrowing src's pointers) */
        d->data.rewrite_cond.cond_pattern = NULL;
        d->data.rewrite_cond.flags_raw = NULL;
        if (src->data.rewrite_cond.cond_pattern) {
            d->data.rewrite_cond.cond_pattern = strdup(src->data.rewrite_cond.cond_pattern);
            if (!d->data.rewrite_cond.cond_pattern) goto fail;
        }
        if (src->data.rewrite_cond.flags_raw) {
            d->data.rewrite_cond.flags_raw = strdup(src->data.rewrite_cond.flags_raw);
            if (!d->data.rewrite_cond.flags_raw) goto fail;
        }
        break;
    case DIR_REWRITE_RULE:
        d->data.rewrite_rule.pattern = NULL;
        d->data.rewrite_rule.flags_raw = NULL;
        d->data.rewrite_rule.conditions = NULL;
        if (src->data.rewrite_rule.pattern) {
            d->data.rewrite_rule.pattern = strdup(src->data.rewrite_rule.pattern);
            if (!d->data.rewrite_rule.pattern) goto fail;
        }
        if (src->data.rewrite_rule.flags_raw) {
            d->data.rewrite_rule.flags_raw = strdup(src->data.rewrite_rule.flags_raw);
            if (!d->data.rewrite_rule.flags_raw) goto fail;
        }
        if (src->data.rewrite_rule.conditions) {
            d->data.rewrite_rule.conditions = copy_directive_list(src->data.rewrite_rule.conditions);
            if (!d->data.rewrite_rule.conditions) goto fail;
        }
        break;
    case DIR_REWRITE_MAP:
        d->data.rewrite_map.map_name = NULL;
        d->data.rewrite_map.map_type = NULL;
        d->data.rewrite_map.map_source = NULL;
        if (src->data.rewrite_map.map_name) {
            d->data.rewrite_map.map_name = strdup(src->data.rewrite_map.map_name);
            if (!d->data.rewrite_map.map_name) goto fail;
        }
        if (src->data.rewrite_map.map_type) {
            d->data.rewrite_map.map_type = strdup(src->data.rewrite_map.map_type);
            if (!d->data.rewrite_map.map_type) goto fail;
        }
        if (src->data.rewrite_map.map_source) {
            d->data.rewrite_map.map_source = strdup(src->data.rewrite_map.map_source);
            if (!d->data.rewrite_map.map_source) goto fail;
        }
        break;
    case DIR_IF:
    case DIR_ELSEIF:
        d->data.if_block.condition = NULL;
        d->data.if_block.children = NULL;
        if (src->data.if_block.condition) {
            d->data.if_block.condition = expr_clone((const expr_node_t *)src->data.if_block.condition);
            if (!d->data.if_block.condition) goto fail;
        }
        if (src->data.if_block.children) {
            d->data.if_block.children = copy_directive_list(src->data.if_block.children);
            if (!d->data.if_block.children) goto fail;
        }
        break;
    case DIR_ELSE:
        d->data.if_block.condition = NULL;
        d->data.if_block.children = NULL;
        if (src->data.if_block.children) {
            d->data.if_block.children = copy_directive_list(src->data.if_block.children);
            if (!d->data.if_block.children) goto fail;
        }
        break;
    default:
        break;
    }

    return d;

fail:
    /* Clean up partially allocated directive on failure */
    htaccess_directives_free(d);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Internal: deep-copy a directive list                                */
/* ------------------------------------------------------------------ */
static htaccess_directive_t *copy_directive_list(const htaccess_directive_t *src)
{
    htaccess_directive_t *head = NULL;
    htaccess_directive_t *tail = NULL;

    for (const htaccess_directive_t *s = src; s; s = s->next) {
        htaccess_directive_t *d = copy_directive(s);
        if (!d) {
            htaccess_directives_free(head);
            return NULL;
        }
        if (!head)
            head = d;
        else
            tail->next = d;
        tail = d;
    }
    return head;
}

/* ------------------------------------------------------------------ */
/* Internal: check if two directives match for override purposes       */
/*                                                                     */
/* "Same type" means same directive_type_t AND same identifying key.   */
/* For Header directives, the key is the header name.                  */
/* For PHP directives, the key is the ini setting name.                */
/* For most others, just the type is sufficient.                       */
/* ------------------------------------------------------------------ */
static int directives_match_for_override(const htaccess_directive_t *a,
                                         const htaccess_directive_t *b)
{
    if (a->type != b->type)
        return 0;

    switch (a->type) {
    /* Header directives: match by header name */
    case DIR_HEADER_SET:
    case DIR_HEADER_UNSET:
    case DIR_HEADER_APPEND:
    case DIR_HEADER_MERGE:
    case DIR_HEADER_ADD:
    case DIR_REQUEST_HEADER_SET:
    case DIR_REQUEST_HEADER_UNSET:
        if (a->name && b->name)
            return strcmp(a->name, b->name) == 0;
        return 0;

    /* PHP directives: match by ini setting name */
    case DIR_PHP_VALUE:
    case DIR_PHP_FLAG:
    case DIR_PHP_ADMIN_VALUE:
    case DIR_PHP_ADMIN_FLAG:
        if (a->name && b->name)
            return strcmp(a->name, b->name) == 0;
        return 0;

    /* SetEnv: match by variable name */
    case DIR_SETENV:
        if (a->name && b->name)
            return strcmp(a->name, b->name) == 0;
        return 0;

    /* ExpiresByType: match by MIME type */
    case DIR_EXPIRES_BY_TYPE:
        if (a->name && b->name)
            return strcmp(a->name, b->name) == 0;
        return 0;

    /* ErrorDocument: match by error code */
    case DIR_ERROR_DOCUMENT:
        return a->data.error_doc.error_code == b->data.error_doc.error_code;

    /* For these, just matching by type is enough (singleton directives) */
    case DIR_ORDER:
    case DIR_EXPIRES_ACTIVE:
    case DIR_EXPIRES_DEFAULT:
    case DIR_DIRECTORY_INDEX:
    case DIR_BRUTE_FORCE_PROTECTION:
    case DIR_BRUTE_FORCE_ALLOWED_ATTEMPTS:
    case DIR_BRUTE_FORCE_WINDOW:
    case DIR_BRUTE_FORCE_ACTION:
    case DIR_BRUTE_FORCE_THROTTLE_DURATION:
        return 1;

    /* Allow/Deny: match by value (CIDR or "all") */
    case DIR_ALLOW_FROM:
    case DIR_DENY_FROM:
        if (a->value && b->value)
            return strcmp(a->value, b->value) == 0;
        return 0;

    /* Redirect: match by source path */
    case DIR_REDIRECT:
        if (a->name && b->name)
            return strcmp(a->name, b->name) == 0;
        return 0;

    /* RedirectMatch: match by pattern */
    case DIR_REDIRECT_MATCH:
        if (a->data.redirect.pattern && b->data.redirect.pattern)
            return strcmp(a->data.redirect.pattern,
                          b->data.redirect.pattern) == 0;
        return 0;

    /* FilesMatch: accumulate (parent + child rules both apply) */
    case DIR_FILES_MATCH:
        return 0;

    /* SetEnvIf/SetEnvIfNoCase/BrowserMatch: match by attribute + pattern + name */
    case DIR_SETENVIF:
    case DIR_SETENVIF_NOCASE:
    case DIR_BROWSER_MATCH:
        if (a->name && b->name &&
            a->data.envif.attribute && b->data.envif.attribute &&
            a->data.envif.pattern && b->data.envif.pattern)
            return strcmp(a->name, b->name) == 0 &&
                   strcmp(a->data.envif.attribute, b->data.envif.attribute) == 0 &&
                   strcmp(a->data.envif.pattern, b->data.envif.pattern) == 0;
        return 0;

    /* RewriteEngine / RewriteBase: singleton (child overrides parent) */
    case DIR_REWRITE_ENGINE:
    case DIR_REWRITE_BASE:
    case DIR_ADD_DEFAULT_CHARSET:
    case DIR_DEFAULT_TYPE:
    case DIR_SATISFY:
        return 1;

    /* RewriteRule / RewriteCond: accumulate (never override) */
    case DIR_REWRITE_RULE:
    case DIR_REWRITE_COND:
        return 0;

    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Internal: merge child directives into parent list                   */
/*                                                                     */
/* For each child directive, if a matching parent directive exists,     */
/* replace it. Otherwise, append the child directive to the end.       */
/* Returns the merged list (may be a new head).                        */
/* ------------------------------------------------------------------ */
static htaccess_directive_t *merge_directives(htaccess_directive_t *parent,
                                              const htaccess_directive_t *child)
{
    if (!child)
        return parent;
    if (!parent)
        return copy_directive_list(child);

    /* Pre-compute tail pointer to avoid O(n) scan on each append */
    htaccess_directive_t *merge_tail = parent;
    while (merge_tail->next)
        merge_tail = merge_tail->next;

    for (const htaccess_directive_t *c = child; c; c = c->next) {
        /* Check if this child directive overrides an existing parent */
        int replaced = 0;
        htaccess_directive_t *prev = NULL;
        for (htaccess_directive_t *p = parent; p; p = p->next) {
            if (directives_match_for_override(p, c)) {
                /* Replace parent directive in-place with child copy */
                htaccess_directive_t *replacement = copy_directive(c);
                if (!replacement) break;
                replacement->next = p->next;
                if (prev)
                    prev->next = replacement;
                else
                    parent = replacement;
                /* Update tail if we replaced the tail node */
                if (p == merge_tail)
                    merge_tail = replacement;
                /* Free the old parent node */
                p->next = NULL;
                htaccess_directives_free(p);
                replaced = 1;
                break;
            }
            prev = p;
        }

        if (!replaced) {
            /* Append child directive to end of parent list */
            htaccess_directive_t *copy = copy_directive(c);
            if (!copy) continue;
            merge_tail->next = copy;
            merge_tail = copy;
        }
    }

    return parent;
}

/* ------------------------------------------------------------------ */
/* Internal: try to read and parse a file, then cache it               */
/* ------------------------------------------------------------------ */
static htaccess_directive_t *read_and_cache(const char *htaccess_path)
{
    FILE *fp = fopen(htaccess_path, "r");
    if (!fp)
        return NULL;

    /* fstat on the opened fd — avoids TOCTOU race between stat and fopen */
    struct stat st;
    if (fstat(fileno(fp), &st) != 0) {
        fclose(fp);
        return NULL;
    }

    /* Read entire file */
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0) {
        fclose(fp);
        return NULL;
    }

    if (fsize > htaccess_size_limit) {
        lsi_log(NULL, LSI_LOG_WARN,
                "[htaccess] %s: file too large (%ld bytes, limit %d), skipping",
                htaccess_path, fsize, htaccess_size_limit);
        fclose(fp);
        return NULL;
    }

    char *content = malloc((size_t)fsize + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }

    size_t nread = fread(content, 1, (size_t)fsize, fp);
    int read_err = ferror(fp);
    fclose(fp);

    if (nread != (size_t)fsize || read_err) {
        lsi_log(NULL, LSI_LOG_WARN,
                "[htaccess] short read or I/O error on %s (got %zu of %ld)",
                htaccess_path, nread, (long)fsize);
        free(content);
        return NULL;
    }
    content[nread] = '\0';

    /* Parse with timing — adaptive size limit (Apache behavior) */
    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);
    htaccess_directive_t *dirs = htaccess_parse(content, nread, htaccess_path);
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    free(content);

    long elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1000 +
                      (t_end.tv_nsec - t_start.tv_nsec) / 1000000;
    if (elapsed_ms > 2500 && fsize > MAX_HTACCESS_FILE_SIZE_FLOOR) {
        int new_limit = (int)(fsize / 2);
        if (new_limit < MAX_HTACCESS_FILE_SIZE_FLOOR)
            new_limit = MAX_HTACCESS_FILE_SIZE_FLOOR;
        if (new_limit < htaccess_size_limit) {
            htaccess_size_limit = new_limit;
            lsi_log(NULL, LSI_LOG_WARN,
                    "[htaccess] %s: parse took %ldms, reducing size limit to %d",
                    htaccess_path, elapsed_ms, htaccess_size_limit);
        }
    }

    /* Try to cache the result (cache takes ownership of dirs on success).
     * Use st_ino ^ st_blocks as cache key (must match dirwalk lookup). */
    ino_t cache_ino = st.st_ino ^ (ino_t)st.st_blocks;
    int put_rc = htaccess_cache_put(htaccess_path, st.st_mtime,
                                    st.st_size, cache_ino, dirs);
    if (put_rc != 0) {
        /* Cache full or error — caller still owns dirs, return directly */
        return dirs;
    }

    /* Cache took ownership; return pointer via cache_get */
    htaccess_directive_t *cached = NULL;
    htaccess_cache_get(htaccess_path, st.st_mtime, st.st_size, cache_ino, &cached);
    return cached;
}

/* ------------------------------------------------------------------ */
/* Internal: filter directives by AllowOverride bitmask                */
/* ------------------------------------------------------------------ */

/**
 * Filter a directive list by AllowOverride bitmask.
 * Removes directives whose category is not permitted.
 * Recursively filters container children.
 * Returns the filtered list (may be NULL if all removed).
 * Frees removed nodes.
 */
static htaccess_directive_t *filter_by_allow_override(
    htaccess_directive_t *head, int allow_override, const char *path)
{
    htaccess_directive_t *result = NULL;
    htaccess_directive_t *tail = NULL;
    htaccess_directive_t *cur = head;

    while (cur) {
        htaccess_directive_t *next = cur->next;
        cur->next = NULL;

        int cat = directive_category(cur->type);
        if (cat != ALLOW_OVERRIDE_ALL && (allow_override & cat) != cat) {
            /* Directive not permitted — log and free */
            lsi_log(NULL, LSI_LOG_WARN,
                    "[htaccess] %s:%d: directive blocked by AllowOverride",
                    path ? path : "<unknown>", cur->line_number);
            htaccess_directives_free(cur);
        } else {
            /* Recursively filter container children */
            switch (cur->type) {
            case DIR_FILES_MATCH:
                cur->data.files_match.children = filter_by_allow_override(
                    cur->data.files_match.children, allow_override, path);
                break;
            case DIR_FILES:
                cur->data.files.children = filter_by_allow_override(
                    cur->data.files.children, allow_override, path);
                break;
            case DIR_IFMODULE:
                cur->data.ifmodule.children = filter_by_allow_override(
                    cur->data.ifmodule.children, allow_override, path);
                break;
            case DIR_REQUIRE_ANY_OPEN:
            case DIR_REQUIRE_ALL_OPEN:
                cur->data.require_container.children = filter_by_allow_override(
                    cur->data.require_container.children, allow_override, path);
                /* Remove empty containers to prevent spurious deny */
                if (!cur->data.require_container.children) {
                    htaccess_directives_free(cur);
                    goto next_directive;
                }
                break;
            case DIR_LIMIT:
            case DIR_LIMIT_EXCEPT:
                cur->data.limit.children = filter_by_allow_override(
                    cur->data.limit.children, allow_override, path);
                break;
            case DIR_REWRITE_RULE:
                /* Conditions are owned by the rule, filter them too */
                cur->data.rewrite_rule.conditions = filter_by_allow_override(
                    cur->data.rewrite_rule.conditions, allow_override, path);
                break;
            case DIR_IF:
            case DIR_ELSEIF:
            case DIR_ELSE:
                cur->data.if_block.children = filter_by_allow_override(
                    cur->data.if_block.children, allow_override, path);
                break;
            default:
                break;
            }

            /* Append to result */
            if (!result)
                result = cur;
            else
                tail->next = cur;
            tail = cur;
        }

next_directive:
        cur = next;
    }

    return result;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/**
 * Recursively flatten DIR_IFMODULE in a directive list.
 * IfModule containers are spliced out, their children promoted in-place.
 * Then recurse into container children (Files, FilesMatch, If, Limit, etc.)
 */
static htaccess_directive_t *flatten_ifmodule(htaccess_directive_t *head)
{
    htaccess_directive_t *flat_head = NULL;
    htaccess_directive_t *flat_tail = NULL;
    htaccess_directive_t *cur = head;

    while (cur) {
        htaccess_directive_t *next = cur->next;
        if (cur->type == DIR_IFMODULE) {
            /* Splice children (recursively flatten them too) */
            htaccess_directive_t *ic = flatten_ifmodule(cur->data.ifmodule.children);
            if (ic) {
                if (!flat_head) flat_head = ic;
                else flat_tail->next = ic;
                while (ic->next) ic = ic->next;
                flat_tail = ic;
            }
            cur->data.ifmodule.children = NULL;
            cur->next = NULL;
            htaccess_directives_free(cur);
        } else {
            cur->next = NULL;
            if (!flat_head) flat_head = cur;
            else flat_tail->next = cur;
            flat_tail = cur;

            /* Recurse into container children */
            htaccess_directive_t **cp = NULL;
            switch (cur->type) {
            case DIR_FILES:       cp = &cur->data.files.children; break;
            case DIR_FILES_MATCH: cp = &cur->data.files_match.children; break;
            case DIR_IF: case DIR_ELSEIF: case DIR_ELSE:
                                  cp = &cur->data.if_block.children; break;
            case DIR_LIMIT: case DIR_LIMIT_EXCEPT:
                                  cp = &cur->data.limit.children; break;
            case DIR_REQUIRE_ANY_OPEN: case DIR_REQUIRE_ALL_OPEN:
                                  cp = &cur->data.require_container.children; break;
            default: break;
            }
            if (cp && *cp)
                *cp = flatten_ifmodule(*cp);
        }
        cur = next;
    }
    return flat_head;
}

htaccess_directive_t *htaccess_dirwalk(lsi_session_t *session,
                                       const char *doc_root,
                                       const char *target_dir)
{
    /* Configurable AccessFileName via env var (default: .htaccess).
     * OLS stores this per-vhost; we use an env var for simplicity.
     * Reject path separators and ".." to prevent traversal. */
    /* Read access filename per request (not cached — supports multi-vhost) */
    const char *access_filename = ".htaccess";
    {
        const char *env = getenv("HTACCESS_ACCESS_FILENAME");
        if (env && env[0] && !strchr(env, '/') && !strchr(env, '\\') &&
            !strstr(env, ".."))
            access_filename = env;
    }

    /* HTACCESS_VHROOT: virtual host root for three-level merge.
     * If set and valid, the dirwalk starts from vhroot instead of doc_root,
     * enabling .htaccess files between vhroot and doc_root to be picked up.
     * Validation: must start with '/', no '..', must be a prefix of doc_root. */
    const char *vhroot = NULL;
    {
        const char *env = getenv("HTACCESS_VHROOT");
        if (env && env[0] == '/' && !strstr(env, "..")) {
            vhroot = env;
        }
    }

    /* Periodically flush negative stat cache so newly created
     * .htaccess files are discovered within ~256 requests */
    if (++neg_stat_generation >= NEG_STAT_TTL) {
        neg_stat_flush();
        neg_stat_generation = 0;
    }

    if (!doc_root || !target_dir)
        return NULL;

    /* Determine walk_root: use vhroot if valid and is a prefix of doc_root */
    const char *walk_root = doc_root;
    if (vhroot) {
        size_t vh_len = strlen(vhroot);
        /* Strip trailing slash from vhroot for comparison */
        while (vh_len > 1 && vhroot[vh_len - 1] == '/')
            vh_len--;
        size_t dr_len = strlen(doc_root);
        /* vhroot must be a proper prefix of doc_root at a path boundary */
        if (vh_len <= dr_len &&
            strncmp(vhroot, doc_root, vh_len) == 0 &&
            (doc_root[vh_len] == '/' || doc_root[vh_len] == '\0')) {
            walk_root = vhroot;
        }
    }

    size_t root_len = strlen(walk_root);
    size_t target_len = strlen(target_dir);

    /* Strip trailing slashes from walk_root for comparison */
    while (root_len > 1 && walk_root[root_len - 1] == '/')
        root_len--;

    /* target_dir must start with walk_root at a path segment boundary.
     * Prevent /var/www2 matching /var/www prefix. */
    if (target_len < root_len)
        return NULL;
    if (strncmp(walk_root, target_dir, root_len) != 0)
        return NULL;
    /* After the prefix, next char must be / or \0 */
    if (target_len > root_len && target_dir[root_len] != '/')
        return NULL;

    /* Small stack array for typical path depth; heap fallback for deep trees */
#define STACK_DIR_DEPTH 6
    char stack_paths[STACK_DIR_DEPTH][MAX_PATH_LEN];
    char (*paths)[MAX_PATH_LEN] = stack_paths;
    int paths_cap = STACK_DIR_DEPTH;
    int heap_paths = 0;

    int num_paths = 0;

    /* Build all directory paths from root to target */
    char current_path[MAX_PATH_LEN];
    if (root_len >= MAX_PATH_LEN) {
        return NULL;
    }

    memcpy(current_path, walk_root, root_len);
    current_path[root_len] = '\0';

    /* Add walk_root */
    memcpy(paths[num_paths], current_path, root_len + 1);
    num_paths++;

    /* Walk remaining components */
    const char *rest = target_dir + root_len;
    while (*rest && num_paths < MAX_DIR_DEPTH) {
        if (*rest == '/') {
            rest++;
            continue;
        }
        const char *slash = strchr(rest, '/');
        size_t comp_len = slash ? (size_t)(slash - rest) : strlen(rest);

        /* Reject ".." path components — defense-in-depth against traversal */
        if (comp_len == 2 && rest[0] == '.' && rest[1] == '.') {
            if (heap_paths) free(paths);
            return NULL;
        }

        size_t cur_len = strlen(current_path);
        if (cur_len + 1 + comp_len >= MAX_PATH_LEN)
            break;

        current_path[cur_len] = '/';
        memcpy(current_path + cur_len + 1, rest, comp_len);
        current_path[cur_len + 1 + comp_len] = '\0';

        /* Grow to heap if stack array is full */
        if (num_paths >= paths_cap && !heap_paths) {
            paths = malloc((size_t)MAX_DIR_DEPTH * MAX_PATH_LEN);
            if (!paths) return NULL;
            memcpy(paths, stack_paths,
                   (size_t)STACK_DIR_DEPTH * MAX_PATH_LEN);
            paths_cap = MAX_DIR_DEPTH;
            heap_paths = 1;
        }

        memcpy(paths[num_paths], current_path, cur_len + 1 + comp_len + 1);
        num_paths++;

        rest = slash ? slash + 1 : rest + comp_len;
    }

    /* Now process each directory level */
    htaccess_directive_t *merged = NULL;

    /* Read AllowOverride configuration (env var set by admin in vhconf.conf).
     * Default ALLOW_OVERRIDE_ALL (255) for backward compatibility. */
    int allow_override = ALLOW_OVERRIDE_ALL;
    if (session) {
        int val_len = 0;
        const char *ao_val = lsi_session_get_env(session,
            "HTACCESS_ALLOW_OVERRIDE", 23, &val_len);
        if (ao_val && val_len > 0) {
            int v = atoi(ao_val);
            if (v >= 0 && v <= 255)
                allow_override = v;
        }
    }

    for (int i = 0; i < num_paths; i++) {
        /* Construct .htaccess path for this directory */
        char htaccess_path[MAX_PATH_LEN];
        int written = snprintf(htaccess_path, MAX_PATH_LEN,
                               "%s/%s", paths[i], access_filename);
        if (written < 0 || written >= MAX_PATH_LEN)
            continue;

        /* Stat the file for cache validation metadata */
        htaccess_directive_t *level_dirs = NULL;
        struct stat st;
        int have_stat;

        /* Skip stat if negative cache says this path has no .htaccess */
        if (neg_stat_check(htaccess_path)) {
            have_stat = 0;
        } else {
            have_stat = (stat(htaccess_path, &st) == 0);
            if (!have_stat)
                neg_stat_add(htaccess_path);
        }

        time_t mtime = have_stat ? st.st_mtime : 0;
        off_t  fsize = have_stat ? st.st_size  : 0;
        ino_t  inode = have_stat ? st.st_ino   : 0;
        /* Use st_blocks as additional change indicator (improves change detection accuracy).
         * More reliable than mtime alone on NFS/containers. Encode into
         * the inode field to avoid changing cache API: inode XOR blocks. */
        if (have_stat)
            inode ^= (ino_t)st.st_blocks;

        /* Try cache first */
        int cache_hit = htaccess_cache_get(htaccess_path, mtime, fsize,
                                           inode, &level_dirs);

        if (cache_hit != 0 && have_stat) {
            /* Cache miss but file exists — read and parse */
            level_dirs = read_and_cache(htaccess_path);
            if (level_dirs)
                neg_stat_remove(htaccess_path);
        }

        if (level_dirs) {
            /* Merge this level's directives into the accumulated result.
             * Note: merge_directives deep-copies from level_dirs, so cached
             * data is never modified. AllowOverride filtering is applied
             * after the merge loop on the final result (which we own). */
            merged = merge_directives(merged, level_dirs);
        }
        /* If no directives at this level, skip (doesn't affect inheritance) */
    }

    if (heap_paths)
        free(paths);

    /* Apply AllowOverride filter to the merged result.
     * merged is a freshly allocated deep copy — safe to modify in place. */
    if (merged && allow_override != ALLOW_OVERRIDE_ALL)
        merged = filter_by_allow_override(merged, allow_override, "AllowOverride");

    /* Flatten IfModule containers — expand ALL blocks unconditionally.
     *
     * OLS cannot query Apache's module registry, so we cannot determine
     * whether a module is actually loaded. CyberPanel takes the same approach:
     * treat all IfModule blocks (both positive and negated) as TRUE.
     *
     * This means:
     *   <IfModule mod_headers.c>   → expand (module assumed loaded)
     *   <IfModule !mod_headers.c>  → expand (cannot verify, safer to include)
     *
     * Rationale: dropping negated blocks breaks real configs like:
     *   <IfModule !mod_nonexistent.c>
     *     Header set X-Fallback "yes"
     *   </IfModule>
     * which should always execute since mod_nonexistent doesn't exist. */
    merged = flatten_ifmodule(merged);

    return merged;
}
