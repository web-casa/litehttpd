/**
 * htaccess_directive.c - Directive memory management
 *
 * Implements htaccess_directives_free() which walks a directive linked list
 * and releases all dynamically allocated memory, including type-specific
 * strings and nested children (FilesMatch).
 *
 * Validates: Requirements 2.2
 */
#include "htaccess_directive.h"
#include "htaccess_expr.h"
#include <stdlib.h>

/**
 * Free a single directive node's owned memory (not the node itself).
 */
static void directive_free_fields(htaccess_directive_t *dir)
{
    if (!dir)
        return;

    free(dir->name);
    free(dir->value);
    free(dir->env_condition);

    switch (dir->type) {
    case DIR_REDIRECT:
    case DIR_REDIRECT_MATCH:
        free(dir->data.redirect.pattern);
        break;

    case DIR_FILES_MATCH:
        free(dir->data.files_match.pattern);
        /* Recursively free nested children */
        htaccess_directives_free(dir->data.files_match.children);
        break;

    case DIR_SETENVIF:
    case DIR_SETENVIF_NOCASE:
    case DIR_BROWSER_MATCH:
        free(dir->data.envif.attribute);
        free(dir->data.envif.pattern);
        break;

    /* v2 container types — recursively free children */
    case DIR_IFMODULE:
        htaccess_directives_free(dir->data.ifmodule.children);
        break;

    case DIR_FILES:
        htaccess_directives_free(dir->data.files.children);
        break;

    case DIR_REQUIRE_ANY_OPEN:
    case DIR_REQUIRE_ALL_OPEN:
        htaccess_directives_free(dir->data.require_container.children);
        break;

    case DIR_LIMIT:
    case DIR_LIMIT_EXCEPT:
        free(dir->data.limit.methods);
        htaccess_directives_free(dir->data.limit.children);
        break;

    case DIR_HEADER_EDIT:
    case DIR_HEADER_EDIT_STAR:
    case DIR_HEADER_ALWAYS_EDIT:
    case DIR_HEADER_ALWAYS_EDIT_STAR:
        free(dir->data.header_ext.edit_pattern);
        break;

    case DIR_REWRITE_COND:
        free(dir->data.rewrite_cond.cond_pattern);
        free(dir->data.rewrite_cond.flags_raw);
        break;

    case DIR_REWRITE_RULE:
        free(dir->data.rewrite_rule.pattern);
        free(dir->data.rewrite_rule.flags_raw);
        htaccess_directives_free(dir->data.rewrite_rule.conditions);
        break;

    case DIR_REWRITE_MAP:
        free(dir->data.rewrite_map.map_name);
        free(dir->data.rewrite_map.map_type);
        free(dir->data.rewrite_map.map_source);
        break;

    case DIR_IF:
    case DIR_ELSEIF:
        if (dir->data.if_block.condition) {
            expr_free((expr_node_t *)dir->data.if_block.condition);
        }
        htaccess_directives_free(dir->data.if_block.children);
        break;
    case DIR_ELSE:
        htaccess_directives_free(dir->data.if_block.children);
        break;

    default:
        /* No additional heap allocations for other types */
        break;
    }
}

void htaccess_directives_free(htaccess_directive_t *head)
{
    htaccess_directive_t *cur = head;
    while (cur) {
        htaccess_directive_t *next = cur->next;
        directive_free_fields(cur);
        free(cur);
        cur = next;
    }
}

int directive_category(directive_type_t type)
{
    switch (type) {
    /* Limit category */
    case DIR_ORDER:
    case DIR_ALLOW_FROM:
    case DIR_DENY_FROM:
    case DIR_LIMIT:
    case DIR_LIMIT_EXCEPT:
        return ALLOW_OVERRIDE_LIMIT;

    /* Auth category */
    case DIR_AUTH_TYPE:
    case DIR_AUTH_NAME:
    case DIR_AUTH_USER_FILE:
    case DIR_REQUIRE_VALID_USER:
    case DIR_REQUIRE_ALL_GRANTED:
    case DIR_REQUIRE_ALL_DENIED:
    case DIR_REQUIRE_IP:
    case DIR_REQUIRE_NOT_IP:
    case DIR_REQUIRE_ANY_OPEN:
    case DIR_REQUIRE_ALL_OPEN:
    case DIR_REQUIRE_ENV:
    case DIR_SATISFY:
        return ALLOW_OVERRIDE_AUTH;

    /* FileInfo category */
    case DIR_ADD_TYPE:
    case DIR_ADD_HANDLER:
    case DIR_SET_HANDLER:
    case DIR_FORCE_TYPE:
    case DIR_ADD_CHARSET:
    case DIR_ADD_ENCODING:
    case DIR_ADD_DEFAULT_CHARSET:
    case DIR_DEFAULT_TYPE:
    case DIR_REMOVE_TYPE:
    case DIR_REMOVE_HANDLER:
    case DIR_ACTION:
    case DIR_REDIRECT:
    case DIR_REDIRECT_MATCH:
    case DIR_ERROR_DOCUMENT:
    case DIR_REWRITE_ENGINE:
    case DIR_REWRITE_BASE:
    case DIR_REWRITE_COND:
    case DIR_REWRITE_RULE:
    case DIR_REWRITE_OPTIONS:
    case DIR_REWRITE_MAP:
        return ALLOW_OVERRIDE_FILEINFO;

    /* Indexes category */
    case DIR_DIRECTORY_INDEX:
        return ALLOW_OVERRIDE_INDEXES;

    /* If/ElseIf/Else — always allowed (module extension) */
    case DIR_IF:
    case DIR_ELSEIF:
    case DIR_ELSE:
        return ALLOW_OVERRIDE_ALL;

    /* Options category */
    case DIR_OPTIONS:
        return ALLOW_OVERRIDE_OPTIONS | ALLOW_OVERRIDE_INDEXES;
    case DIR_EXPIRES_ACTIVE:
    case DIR_EXPIRES_BY_TYPE:
    case DIR_EXPIRES_DEFAULT:
        return ALLOW_OVERRIDE_OPTIONS;

    /* Unrestricted — module extensions, containers, headers, PHP, env, brute force */
    default:
        return ALLOW_OVERRIDE_ALL;
    }
}
