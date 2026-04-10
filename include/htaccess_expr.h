/**
 * htaccess_expr.h - Apache ap_expr expression engine (AST-based)
 *
 * Supports:
 *   Comparison: ==, !=, <, >, <=, >=
 *   Regex:      =~, !~
 *   Integer:    -eq, -ne, -lt, -le, -gt, -ge
 *   File tests: -f, -d, -s, -l, -e
 *   IP match:   -ipmatch, -R
 *   Boolean:    &&, ||, !
 *   Parens:     ( expr )
 *   Functions:  tolower(), toupper()
 *   Variables:  %{VAR}, %{HTTP:header}, %{ENV:var}
 *
 * Used by <If>, <ElseIf> conditional blocks.
 */
#ifndef HTACCESS_EXPR_H
#define HTACCESS_EXPR_H

#include "ls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * AST node type enumeration.
 */
typedef enum {
    /* Leaf nodes */
    NODE_STRING,         /* String literal 'value' or "value" */
    NODE_VAR,            /* Variable reference %{VAR} */
    NODE_REGEX,          /* Regex /pattern/ or /pattern/i */
    NODE_INTEGER,        /* Integer literal */

    /* Binary comparison operators */
    NODE_CMP_EQ,         /* == */
    NODE_CMP_NE,         /* != */
    NODE_CMP_LT,         /* < */
    NODE_CMP_GT,         /* > */
    NODE_CMP_LE,         /* <= */
    NODE_CMP_GE,         /* >= */
    NODE_REGEX_MATCH,    /* =~ */
    NODE_REGEX_NOMATCH,  /* !~ */

    /* Integer comparison operators */
    NODE_INT_EQ,         /* -eq */
    NODE_INT_NE,         /* -ne */
    NODE_INT_LT,         /* -lt */
    NODE_INT_LE,         /* -le */
    NODE_INT_GT,         /* -gt */
    NODE_INT_GE,         /* -ge */

    /* Boolean operators */
    NODE_AND,            /* && */
    NODE_OR,             /* || */
    NODE_NOT,            /* ! (unary, operand in left) */

    /* Unary file test operators (operand in left) */
    NODE_FILE_F,         /* -f (regular file) */
    NODE_FILE_D,         /* -d (directory) */
    NODE_FILE_S,         /* -s (file with size > 0) */
    NODE_FILE_L,         /* -l (symlink) */
    NODE_FILE_E,         /* -e (exists) */

    /* IP match (operand in left = CIDR) */
    NODE_IPMATCH,        /* -ipmatch / -R */

    /* Function calls (operand in left) */
    NODE_FUNC_TOLOWER,   /* tolower() */
    NODE_FUNC_TOUPPER,   /* toupper() */
} expr_node_type_t;

/**
 * AST node structure for expression trees.
 */
typedef struct expr_node {
    expr_node_type_t   type;
    char              *str_val;   /* String/variable/regex/integer value */
    struct expr_node  *left;      /* Left child (binary) or operand (unary) */
    struct expr_node  *right;     /* Right child (binary only) */
} expr_node_t;

/* Backward-compatible typedef */
typedef expr_node_t htaccess_expr_t;

/**
 * Parse an expression string into an AST.
 * Returns NULL on parse error.
 * Caller must free with expr_free().
 */
expr_node_t *expr_parse(const char *text);

/**
 * Evaluate an expression AST against the current request.
 * Returns 1 if true, 0 if false.
 * Uses short-circuit evaluation for && and ||.
 */
int expr_eval(lsi_session_t *session, const expr_node_t *node);

/**
 * Deep-copy an expression AST.
 * Returns NULL on allocation failure.
 */
expr_node_t *expr_clone(const expr_node_t *node);

/**
 * Convert an expression AST back to text representation.
 * Caller must free() the returned string.
 * Returns NULL on allocation failure.
 */
char *expr_to_string(const expr_node_t *node);

/**
 * Expand %{VAR} references in a string.
 * Returns a static thread-local buffer.
 */
const char *expr_expand_var(lsi_session_t *session, const char *var_ref);

/**
 * Free an expression AST recursively.
 */
void expr_free(expr_node_t *node);

/* ------------------------------------------------------------------ */
/*  Backward compatibility: old API function names                     */
/* ------------------------------------------------------------------ */

#define parse_expr(text)           expr_parse(text)
#define eval_expr(session, expr)   expr_eval(session, (const expr_node_t *)(expr))
#define free_expr(expr)            expr_free((expr_node_t *)(expr))

#ifdef __cplusplus
}
#endif

#endif /* HTACCESS_EXPR_H */
