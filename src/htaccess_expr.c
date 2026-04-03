/**
 * htaccess_expr.c - Apache ap_expr expression engine (AST-based)
 *
 * Implements a recursive-descent parser and tree-walking evaluator for
 * Apache-compatible conditional expressions used in <If>/<ElseIf> blocks.
 *
 * Grammar:
 *   expr     -> or_expr
 *   or_expr  -> and_expr ('||' and_expr)*
 *   and_expr -> not_expr ('&&' not_expr)*
 *   not_expr -> '!' not_expr | cmp_expr
 *   cmp_expr -> primary (CMP_OP primary)?
 *   primary  -> '(' expr ')'
 *             | FILE_TEST value
 *             | '-ipmatch' value | '-R' value
 *             | FUNC '(' value ')'
 *             | value
 *   value    -> STRING | VAR | REGEX | INTEGER
 */
#include "htaccess_expr.h"
#include "htaccess_cidr.h"

#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* Maximum expanded variable length */
#define EXPR_VAR_BUF_SIZE 4096

/* Maximum nesting depth for recursive descent */
#define EXPR_MAX_DEPTH 32

/* Maximum regex pattern length */
#define EXPR_MAX_REGEX_LEN 512

/* ================================================================== */
/*  Variable expansion: %{VAR} -> value                                */
/* ================================================================== */

const char *expr_expand_var(lsi_session_t *session, const char *var_ref)
{
    static __thread char buf[EXPR_VAR_BUF_SIZE];

    if (!var_ref || !session)
        return "";

    /* Direct string (no variable reference) */
    if (var_ref[0] != '%' || var_ref[1] != '{')
        return var_ref;

    /* Extract variable name from %{NAME} */
    const char *start = var_ref + 2;
    const char *end = strchr(start, '}');
    if (!end || end == start)
        return "";

    size_t name_len = (size_t)(end - start);
    char name[256];
    if (name_len >= sizeof(name))
        return "";
    memcpy(name, start, name_len);
    name[name_len] = '\0';

    int val_len = 0;

    /* Bounded copy helper */
#define EXPR_COPY_VAL(v, vlen) do { \
    if ((v) && (vlen) > 0) { \
        int _n = (vlen) < (int)(EXPR_VAR_BUF_SIZE - 1) ? (vlen) : (int)(EXPR_VAR_BUF_SIZE - 1); \
        memcpy(buf, (v), _n); buf[_n] = '\0'; return buf; \
    } return ""; \
} while(0)

    /* Standard server variables */
    if (strcmp(name, "REQUEST_URI") == 0) {
        const char *v = lsi_session_get_uri(session, &val_len);
        EXPR_COPY_VAL(v, val_len);
    }
    if (strcmp(name, "QUERY_STRING") == 0) {
        const char *v = lsi_session_get_query_string(session, &val_len);
        EXPR_COPY_VAL(v, val_len);
    }
    if (strcmp(name, "REQUEST_METHOD") == 0) {
        const char *v = lsi_session_get_method(session, &val_len);
        EXPR_COPY_VAL(v, val_len);
    }
    if (strcmp(name, "REMOTE_ADDR") == 0) {
        const char *v = lsi_session_get_client_ip(session, &val_len);
        EXPR_COPY_VAL(v, val_len);
    }
    if (strcmp(name, "SERVER_PORT") == 0) {
        return "80";
    }
    if (strcmp(name, "HTTPS") == 0) {
        const char *v = lsi_session_get_env(session, "HTTPS", 5, &val_len);
        EXPR_COPY_VAL(v, val_len);
    }
    if (strcmp(name, "HTTP_HOST") == 0 || strcmp(name, "SERVER_NAME") == 0) {
        const char *v = lsi_session_get_req_header_by_name(session, "Host", 4, &val_len);
        EXPR_COPY_VAL(v, val_len);
    }
    if (strcmp(name, "REQUEST_FILENAME") == 0) {
        int dr_len = 0, uri_len = 0;
        const char *dr = lsi_session_get_doc_root(session, &dr_len);
        const char *uri = lsi_session_get_uri(session, &uri_len);
        if (dr && uri) {
            if (strstr(uri, ".."))
                return "";
            int n = snprintf(buf, EXPR_VAR_BUF_SIZE, "%.*s%.*s", dr_len, dr, uri_len, uri);
            if (n > 0 && n < (int)EXPR_VAR_BUF_SIZE) return buf;
        }
        return "";
    }

    /* HTTP:header */
    if (strncmp(name, "HTTP:", 5) == 0) {
        const char *hdr_name = name + 5;
        int hdr_len = (int)strlen(hdr_name);
        const char *v = lsi_session_get_req_header_by_name(session, hdr_name, hdr_len, &val_len);
        EXPR_COPY_VAL(v, val_len);
    }

    /* ENV:var */
    if (strncmp(name, "ENV:", 4) == 0) {
        const char *env_name = name + 4;
        int env_len = (int)strlen(env_name);
        const char *v = lsi_session_get_env(session, env_name, env_len, &val_len);
        EXPR_COPY_VAL(v, val_len);
    }

    /* Fallback: try as env var */
    const char *v = lsi_session_get_env(session, name, (int)name_len, &val_len);
    EXPR_COPY_VAL(v, val_len);

    return "";
}

#undef EXPR_COPY_VAL

/* ================================================================== */
/*  Tokenizer                                                          */
/* ================================================================== */

typedef enum {
    TOK_EOF,
    TOK_STRING,           /* 'value' or "value" or unquoted word */
    TOK_VAR,              /* %{NAME} */
    TOK_REGEX,            /* /pattern/ or /pattern/i */
    TOK_INTEGER,          /* numeric literal */

    TOK_AND,              /* && */
    TOK_OR,               /* || */
    TOK_NOT,              /* ! (standalone, not part of != or !~) */

    TOK_EQ,               /* == */
    TOK_NE,               /* != */
    TOK_LT,               /* < */
    TOK_GT,               /* > */
    TOK_LE,               /* <= */
    TOK_GE,               /* >= */
    TOK_REGEX_MATCH,      /* =~ */
    TOK_REGEX_NOMATCH,    /* !~ */

    TOK_INT_EQ,           /* -eq */
    TOK_INT_NE,           /* -ne */
    TOK_INT_LT,           /* -lt */
    TOK_INT_LE,           /* -le */
    TOK_INT_GT,           /* -gt */
    TOK_INT_GE,           /* -ge */

    TOK_FILE_F,           /* -f */
    TOK_FILE_D,           /* -d */
    TOK_FILE_S,           /* -s */
    TOK_FILE_L,           /* -l */
    TOK_FILE_E,           /* -e */

    TOK_IPMATCH,          /* -ipmatch or -R */

    TOK_LPAREN,           /* ( */
    TOK_RPAREN,           /* ) */

    TOK_FUNC_TOLOWER,     /* tolower */
    TOK_FUNC_TOUPPER,     /* toupper */

    TOK_ERROR,
} token_type_t;

typedef struct {
    token_type_t  type;
    char         *value;  /* Allocated string value (if any) */
} token_t;

/** Tokenizer state */
typedef struct {
    const char *input;    /* Current position in input */
    const char *end;      /* End of input */
    token_t     current;  /* Current (lookahead) token */
    token_type_t last_type; /* Previous token type (for regex context) */
    int         error;    /* Error flag */
    int         depth;    /* Nesting depth tracker */
} tokenizer_t;

static void tok_free_value(token_t *tok)
{
    free(tok->value);
    tok->value = NULL;
}

/**
 * Check if a character is valid in an unquoted word (not an operator char).
 */
static int is_word_char(int c)
{
    return c && !isspace((unsigned char)c) &&
           c != '(' && c != ')' &&
           c != '!' && c != '=' && c != '<' && c != '>' &&
           c != '&' && c != '|' && c != '\'' && c != '"';
}

/**
 * Advance tokenizer to produce the next token.
 */
static void tok_next(tokenizer_t *t)
{
    t->last_type = t->current.type;
    tok_free_value(&t->current);

    /* Skip whitespace */
    while (t->input < t->end && isspace((unsigned char)*t->input))
        t->input++;

    if (t->input >= t->end) {
        t->current.type = TOK_EOF;
        return;
    }

    const char *p = t->input;
    char c = *p;

    /* Parentheses */
    if (c == '(') { t->current.type = TOK_LPAREN; t->input++; return; }
    if (c == ')') { t->current.type = TOK_RPAREN; t->input++; return; }

    /* Two-character operators first */
    if (p + 1 < t->end) {
        if (c == '&' && p[1] == '&') { t->current.type = TOK_AND; t->input += 2; return; }
        if (c == '|' && p[1] == '|') { t->current.type = TOK_OR;  t->input += 2; return; }
        if (c == '=' && p[1] == '=') { t->current.type = TOK_EQ;  t->input += 2; return; }
        if (c == '=' && p[1] == '~') { t->current.type = TOK_REGEX_MATCH; t->input += 2; return; }
        if (c == '!' && p[1] == '=') { t->current.type = TOK_NE;  t->input += 2; return; }
        if (c == '!' && p[1] == '~') { t->current.type = TOK_REGEX_NOMATCH; t->input += 2; return; }
        if (c == '<' && p[1] == '=') { t->current.type = TOK_LE;  t->input += 2; return; }
        if (c == '>' && p[1] == '=') { t->current.type = TOK_GE;  t->input += 2; return; }
    }

    /* Single-character operators */
    if (c == '!') { t->current.type = TOK_NOT; t->input++; return; }
    if (c == '<') { t->current.type = TOK_LT;  t->input++; return; }
    if (c == '>') { t->current.type = TOK_GT;  t->input++; return; }

    /* Variable: %{NAME} */
    if (c == '%' && p + 1 < t->end && p[1] == '{') {
        const char *close = memchr(p + 2, '}', t->end - p - 2);
        if (close) {
            size_t len = (size_t)(close + 1 - p);
            t->current.type = TOK_VAR;
            t->current.value = strndup(p, len);
            t->input = close + 1;
            return;
        }
    }

    /* Quoted string: 'value' or "value" */
    if (c == '\'' || c == '"') {
        const char *close = memchr(p + 1, c, t->end - p - 1);
        if (close) {
            t->current.type = TOK_STRING;
            t->current.value = strndup(p + 1, close - p - 1);
            t->input = close + 1;
            return;
        }
        /* Unterminated quote -- treat rest as string */
        t->current.type = TOK_STRING;
        t->current.value = strndup(p + 1, t->end - p - 1);
        t->input = t->end;
        return;
    }

    /* Regex: /pattern/ or /pattern/i -- only after =~ or !~ operators */
    if (c == '/' && (t->last_type == TOK_REGEX_MATCH || t->last_type == TOK_REGEX_NOMATCH)) {
        const char *q = p + 1;
        while (q < t->end) {
            if (*q == '/' && (q == p + 1 || q[-1] != '\\'))
                break;
            q++;
        }
        if (q < t->end) {
            t->current.type = TOK_REGEX;
            t->current.value = strndup(p + 1, q - p - 1);
            t->input = q + 1;
            /* Consume optional flags (i) */
            if (t->input < t->end && *t->input == 'i')
                t->input++;
            return;
        }
    }

    /* Dash keywords: -f, -d, -s, -l, -e, -eq, -ne, -lt, -le, -gt, -ge, -ipmatch, -R */
    if (c == '-' && p + 1 < t->end && isalpha((unsigned char)p[1])) {
        const char *kw = p + 1;
        /* Collect the keyword */
        const char *kw_end = kw;
        while (kw_end < t->end && (isalpha((unsigned char)*kw_end) || *kw_end == '_'))
            kw_end++;
        size_t kw_len = (size_t)(kw_end - kw);

        /* Check if next char after keyword is a word char -- if so, not a keyword */
        int next_is_word = (kw_end < t->end && is_word_char(*kw_end) && *kw_end != '-');

        if (!next_is_word) {
            if (kw_len == 1) {
                switch (*kw) {
                case 'f': t->current.type = TOK_FILE_F; t->input = kw_end; return;
                case 'd': t->current.type = TOK_FILE_D; t->input = kw_end; return;
                case 's': t->current.type = TOK_FILE_S; t->input = kw_end; return;
                case 'l': t->current.type = TOK_FILE_L; t->input = kw_end; return;
                case 'e': t->current.type = TOK_FILE_E; t->input = kw_end; return;
                case 'R': t->current.type = TOK_IPMATCH; t->input = kw_end; return;
                }
            }
            if (kw_len == 2) {
                if (kw[0]=='e' && kw[1]=='q') { t->current.type = TOK_INT_EQ; t->input = kw_end; return; }
                if (kw[0]=='n' && kw[1]=='e') { t->current.type = TOK_INT_NE; t->input = kw_end; return; }
                if (kw[0]=='l' && kw[1]=='t') { t->current.type = TOK_INT_LT; t->input = kw_end; return; }
                if (kw[0]=='l' && kw[1]=='e') { t->current.type = TOK_INT_LE; t->input = kw_end; return; }
                if (kw[0]=='g' && kw[1]=='t') { t->current.type = TOK_INT_GT; t->input = kw_end; return; }
                if (kw[0]=='g' && kw[1]=='e') { t->current.type = TOK_INT_GE; t->input = kw_end; return; }
            }
            if (kw_len == 7 && strncmp(kw, "ipmatch", 7) == 0) {
                t->current.type = TOK_IPMATCH; t->input = kw_end; return;
            }
        }
        /* Not a recognized keyword -- fall through to unquoted word */
    }

    /* Unquoted word: function names (tolower, toupper) or bare strings */
    if (is_word_char(c) || (c == '-' && p + 1 < t->end)) {
        const char *ws = p;
        /* Special case: check for function names before consuming */
        if (strncmp(p, "tolower", 7) == 0 && (p + 7 >= t->end || !isalpha((unsigned char)p[7]))) {
            t->current.type = TOK_FUNC_TOLOWER;
            t->input = p + 7;
            return;
        }
        if (strncmp(p, "toupper", 7) == 0 && (p + 7 >= t->end || !isalpha((unsigned char)p[7]))) {
            t->current.type = TOK_FUNC_TOUPPER;
            t->input = p + 7;
            return;
        }
        if (strncmp(p, "true", 4) == 0 && (p + 4 >= t->end || !isalpha((unsigned char)p[4]))) {
            t->current.type = TOK_STRING;
            t->current.value = strdup("true");
            t->input = p + 4;
            return;
        }
        if (strncmp(p, "false", 5) == 0 && (p + 5 >= t->end || !isalpha((unsigned char)p[5]))) {
            t->current.type = TOK_STRING;
            t->current.value = strdup("false");
            t->input = p + 5;
            return;
        }

        /* Consume unquoted word */
        while (ws < t->end && is_word_char(*ws))
            ws++;

        /* Check if it's a pure integer */
        const char *check = p;
        int is_int = 1;
        if (*check == '-' || *check == '+') check++;
        if (check == ws) is_int = 0;
        for (const char *ci = check; ci < ws && is_int; ci++) {
            if (!isdigit((unsigned char)*ci)) is_int = 0;
        }

        if (is_int) {
            t->current.type = TOK_INTEGER;
        } else {
            t->current.type = TOK_STRING;
        }
        t->current.value = strndup(p, ws - p);
        t->input = ws;
        return;
    }

    /* Unknown character -- error */
    t->current.type = TOK_ERROR;
    t->error = 1;
    t->input++;
}

static void tok_init(tokenizer_t *t, const char *input, size_t len)
{
    t->input = input;
    t->end = input + len;
    t->current.type = TOK_EOF;
    t->current.value = NULL;
    t->last_type = TOK_EOF;
    t->error = 0;
    t->depth = 0;
    tok_next(t);  /* Prime the first token */
}

static void tok_cleanup(tokenizer_t *t)
{
    tok_free_value(&t->current);
}

/* ================================================================== */
/*  AST node helpers                                                   */
/* ================================================================== */

static expr_node_t *node_new(expr_node_type_t type)
{
    expr_node_t *n = calloc(1, sizeof(expr_node_t));
    if (n) n->type = type;
    return n;
}

static expr_node_t *node_leaf(expr_node_type_t type, const char *val)
{
    expr_node_t *n = node_new(type);
    if (n && val) n->str_val = strdup(val);
    return n;
}

static expr_node_t *node_unary(expr_node_type_t type, expr_node_t *operand)
{
    expr_node_t *n = node_new(type);
    if (n) n->left = operand;
    return n;
}

static expr_node_t *node_binary(expr_node_type_t type, expr_node_t *left, expr_node_t *right)
{
    expr_node_t *n = node_new(type);
    if (n) { n->left = left; n->right = right; }
    return n;
}

/* ================================================================== */
/*  Recursive descent parser                                           */
/* ================================================================== */

/* Forward declarations */
static expr_node_t *parse_or(tokenizer_t *t);

/**
 * value -> STRING | VAR | REGEX | INTEGER
 */
static expr_node_t *parse_value(tokenizer_t *t)
{
    expr_node_t *n = NULL;
    switch (t->current.type) {
    case TOK_STRING:
        n = node_leaf(NODE_STRING, t->current.value);
        tok_next(t);
        return n;
    case TOK_VAR:
        n = node_leaf(NODE_VAR, t->current.value);
        tok_next(t);
        return n;
    case TOK_REGEX:
        n = node_leaf(NODE_REGEX, t->current.value);
        tok_next(t);
        return n;
    case TOK_INTEGER:
        n = node_leaf(NODE_INTEGER, t->current.value);
        tok_next(t);
        return n;
    default:
        return NULL;
    }
}

/**
 * primary -> '(' expr ')'
 *          | FILE_TEST value
 *          | '-ipmatch' value | '-R' value
 *          | FUNC '(' value ')'
 *          | value
 */
static expr_node_t *parse_primary(tokenizer_t *t)
{
    /* Parenthesized expression */
    if (t->current.type == TOK_LPAREN) {
        t->depth++;
        if (t->depth > EXPR_MAX_DEPTH) {
            t->error = 1;
            return NULL;
        }
        tok_next(t);
        expr_node_t *inner = parse_or(t);
        if (t->current.type == TOK_RPAREN) {
            tok_next(t);
        } else {
            /* Missing closing paren */
            t->error = 1;
            expr_free(inner);
            return NULL;
        }
        t->depth--;
        return inner;
    }

    /* File tests: -f, -d, -s, -l, -e */
    if (t->current.type >= TOK_FILE_F && t->current.type <= TOK_FILE_E) {
        expr_node_type_t ntype;
        switch (t->current.type) {
        case TOK_FILE_F: ntype = NODE_FILE_F; break;
        case TOK_FILE_D: ntype = NODE_FILE_D; break;
        case TOK_FILE_S: ntype = NODE_FILE_S; break;
        case TOK_FILE_L: ntype = NODE_FILE_L; break;
        case TOK_FILE_E: ntype = NODE_FILE_E; break;
        default: return NULL;
        }
        tok_next(t);
        expr_node_t *operand = parse_value(t);
        if (!operand) {
            t->error = 1;
            return NULL;
        }
        return node_unary(ntype, operand);
    }

    /* IP match: -ipmatch, -R */
    if (t->current.type == TOK_IPMATCH) {
        tok_next(t);
        expr_node_t *operand = parse_value(t);
        if (!operand) {
            t->error = 1;
            return NULL;
        }
        return node_unary(NODE_IPMATCH, operand);
    }

    /* Function calls: tolower(...), toupper(...) */
    if (t->current.type == TOK_FUNC_TOLOWER || t->current.type == TOK_FUNC_TOUPPER) {
        expr_node_type_t ntype = (t->current.type == TOK_FUNC_TOLOWER) ?
                                  NODE_FUNC_TOLOWER : NODE_FUNC_TOUPPER;
        tok_next(t);
        if (t->current.type != TOK_LPAREN) {
            t->error = 1;
            return NULL;
        }
        tok_next(t);
        expr_node_t *arg = parse_or(t);
        if (!arg) {
            t->error = 1;
            return NULL;
        }
        if (t->current.type != TOK_RPAREN) {
            t->error = 1;
            expr_free(arg);
            return NULL;
        }
        tok_next(t);
        return node_unary(ntype, arg);
    }

    /* Plain value */
    return parse_value(t);
}

/**
 * Map comparison token types to AST node types.
 */
static int is_cmp_op(token_type_t tt, expr_node_type_t *out)
{
    switch (tt) {
    case TOK_EQ:            *out = NODE_CMP_EQ;        return 1;
    case TOK_NE:            *out = NODE_CMP_NE;        return 1;
    case TOK_LT:            *out = NODE_CMP_LT;        return 1;
    case TOK_GT:            *out = NODE_CMP_GT;        return 1;
    case TOK_LE:            *out = NODE_CMP_LE;        return 1;
    case TOK_GE:            *out = NODE_CMP_GE;        return 1;
    case TOK_REGEX_MATCH:   *out = NODE_REGEX_MATCH;   return 1;
    case TOK_REGEX_NOMATCH: *out = NODE_REGEX_NOMATCH; return 1;
    case TOK_INT_EQ:        *out = NODE_INT_EQ;        return 1;
    case TOK_INT_NE:        *out = NODE_INT_NE;        return 1;
    case TOK_INT_LT:        *out = NODE_INT_LT;        return 1;
    case TOK_INT_LE:        *out = NODE_INT_LE;        return 1;
    case TOK_INT_GT:        *out = NODE_INT_GT;        return 1;
    case TOK_INT_GE:        *out = NODE_INT_GE;        return 1;
    case TOK_IPMATCH:       *out = NODE_IPMATCH;       return 1;
    default: return 0;
    }
}

/**
 * cmp_expr -> primary (CMP_OP primary)?
 */
static expr_node_t *parse_cmp(tokenizer_t *t)
{
    expr_node_t *left = parse_primary(t);
    if (!left) return NULL;

    expr_node_type_t ntype;
    if (is_cmp_op(t->current.type, &ntype)) {
        /* Special case: -ipmatch after a value becomes binary */
        if (t->current.type == TOK_IPMATCH) {
            tok_next(t);
            expr_node_t *right = parse_value(t);
            if (!right) {
                t->error = 1;
                expr_free(left);
                return NULL;
            }
            return node_binary(NODE_IPMATCH, left, right);
        }
        tok_next(t);
        expr_node_t *right = parse_primary(t);
        if (!right) {
            t->error = 1;
            expr_free(left);
            return NULL;
        }
        return node_binary(ntype, left, right);
    }

    return left;
}

/**
 * not_expr -> '!' not_expr | cmp_expr
 */
static expr_node_t *parse_not(tokenizer_t *t)
{
    if (t->current.type == TOK_NOT) {
        t->depth++;
        if (t->depth > EXPR_MAX_DEPTH) {
            t->error = 1;
            return NULL;
        }
        tok_next(t);
        expr_node_t *inner = parse_not(t);
        t->depth--;
        if (!inner) return NULL;
        return node_unary(NODE_NOT, inner);
    }
    return parse_cmp(t);
}

/**
 * and_expr -> not_expr ('&&' not_expr)*
 */
static expr_node_t *parse_and(tokenizer_t *t)
{
    expr_node_t *left = parse_not(t);
    if (!left) return NULL;

    while (t->current.type == TOK_AND) {
        if (++t->depth > EXPR_MAX_DEPTH) {
            t->error = 1;
            expr_free(left);
            return NULL;
        }
        tok_next(t);
        expr_node_t *right = parse_not(t);
        if (!right) {
            expr_free(left);
            return NULL;
        }
        left = node_binary(NODE_AND, left, right);
    }
    return left;
}

/**
 * or_expr -> and_expr ('||' and_expr)*
 */
static expr_node_t *parse_or(tokenizer_t *t)
{
    expr_node_t *left = parse_and(t);
    if (!left) return NULL;

    while (t->current.type == TOK_OR) {
        if (++t->depth > EXPR_MAX_DEPTH) {
            t->error = 1;
            expr_free(left);
            return NULL;
        }
        tok_next(t);
        expr_node_t *right = parse_and(t);
        if (!right) {
            expr_free(left);
            return NULL;
        }
        left = node_binary(NODE_OR, left, right);
    }
    return left;
}

/* ================================================================== */
/*  Public parse API                                                   */
/* ================================================================== */

expr_node_t *expr_parse(const char *text)
{
    if (!text || !text[0])
        return NULL;

    /* Skip leading whitespace */
    const char *s = text;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return NULL;

    /* Find end, skip trailing whitespace */
    size_t len = strlen(s);
    const char *end = s + len;
    while (end > s && isspace((unsigned char)end[-1])) end--;
    len = (size_t)(end - s);
    if (len == 0) return NULL;

    /* Strip outer quotes if present (Apache puts condition in quotes) */
    if (len >= 2 && s[0] == '"' && end[-1] == '"') {
        s++;
        end--;
        len -= 2;
    }
    if (len == 0) return NULL;

    tokenizer_t tok;
    tok_init(&tok, s, len);

    expr_node_t *ast = parse_or(&tok);

    if (tok.error || (tok.current.type != TOK_EOF && ast)) {
        /* Parse error or unconsumed tokens */
        expr_free(ast);
        tok_cleanup(&tok);
        return NULL;
    }

    tok_cleanup(&tok);

    /* Handle bare value as truthiness test: value != '' */
    if (ast && ast->type <= NODE_INTEGER) {
        /* Wrap: value != '' */
        expr_node_t *empty = node_leaf(NODE_STRING, "");
        expr_node_t *cmp = node_binary(NODE_CMP_NE, ast, empty);
        return cmp;
    }

    return ast;
}

/* ================================================================== */
/*  Evaluator                                                          */
/* ================================================================== */

/**
 * Get the string value of a leaf node, expanding variables if needed.
 */
static const char *eval_node_str(lsi_session_t *session, const expr_node_t *node)
{
    if (!node) return "";

    switch (node->type) {
    case NODE_STRING:
    case NODE_INTEGER:
    case NODE_REGEX:
        return node->str_val ? node->str_val : "";

    case NODE_VAR:
        return expr_expand_var(session, node->str_val);

    case NODE_FUNC_TOLOWER:
    case NODE_FUNC_TOUPPER: {
        const char *inner = eval_node_str(session, node->left);
        if (!inner || !inner[0]) return "";
        static __thread char func_buf[EXPR_VAR_BUF_SIZE];
        size_t slen = strlen(inner);
        if (slen >= EXPR_VAR_BUF_SIZE) slen = EXPR_VAR_BUF_SIZE - 1;
        /* Copy first: inner may alias func_buf if functions are nested */
        char tmp[EXPR_VAR_BUF_SIZE];
        memcpy(tmp, inner, slen);
        for (size_t i = 0; i < slen; i++) {
            func_buf[i] = (node->type == NODE_FUNC_TOLOWER) ?
                          (char)tolower((unsigned char)tmp[i]) :
                          (char)toupper((unsigned char)tmp[i]);
        }
        func_buf[slen] = '\0';
        return func_buf;
    }

    default:
        return "";
    }
}

/**
 * Regex match with thread-local 4-slot cache.
 */
static int eval_regex_match(const char *subject, const char *pattern, int negate)
{
    if (!pattern || !pattern[0])
        return 0;
    if (strlen(pattern) > EXPR_MAX_REGEX_LEN)
        return 0;

    static __thread struct {
        unsigned long hash;
        char *pat;
        regex_t re;
        int valid;
    } re_cache[4];

    unsigned long h = 5381;
    for (const char *p = pattern; *p; p++)
        h = ((h << 5) + h) + (unsigned char)*p;

    regex_t *rep = NULL;
    for (int i = 0; i < 4; i++) {
        if (re_cache[i].valid && re_cache[i].hash == h &&
            strcmp(re_cache[i].pat, pattern) == 0) {
            rep = &re_cache[i].re;
            break;
        }
    }

    if (!rep) {
        int slot = -1;
        for (int i = 0; i < 4; i++) {
            if (!re_cache[i].valid) { slot = i; break; }
        }
        if (slot < 0) {
            regfree(&re_cache[0].re);
            free(re_cache[0].pat);
            for (int i = 0; i < 3; i++)
                re_cache[i] = re_cache[i + 1];
            slot = 3;
            re_cache[slot].valid = 0;
        }
        if (regcomp(&re_cache[slot].re, pattern, REG_EXTENDED | REG_NOSUB) != 0)
            return 0;
        re_cache[slot].hash = h;
        re_cache[slot].pat = strdup(pattern);
        if (!re_cache[slot].pat) {
            regfree(&re_cache[slot].re);
            return 0;
        }
        re_cache[slot].valid = 1;
        rep = &re_cache[slot].re;
    }

    int match = (regexec(rep, subject, 0, NULL, 0) == 0);
    return negate ? !match : match;
}

/**
 * Evaluate a file test on a path.
 */
static int eval_file_test(lsi_session_t *session, expr_node_type_t type, const expr_node_t *operand)
{
    const char *path = eval_node_str(session, operand);
    if (!path || !path[0])
        return 0;

    struct stat st;
    int rc = (type == NODE_FILE_L) ? lstat(path, &st) : stat(path, &st);
    if (rc != 0)
        return 0;

    switch (type) {
    case NODE_FILE_F: return S_ISREG(st.st_mode) ? 1 : 0;
    case NODE_FILE_D: return S_ISDIR(st.st_mode) ? 1 : 0;
    case NODE_FILE_S: return (S_ISREG(st.st_mode) && st.st_size > 0) ? 1 : 0;
    case NODE_FILE_L: return S_ISLNK(st.st_mode) ? 1 : 0;
    case NODE_FILE_E: return 1;  /* stat succeeded, file exists */
    default: return 0;
    }
}

/**
 * Evaluate -ipmatch / -R.
 * Unary form: -ipmatch 'cidr' (matches REMOTE_ADDR against cidr)
 * Binary form: value -ipmatch 'cidr' (matches value against cidr)
 */
static int eval_ipmatch(lsi_session_t *session, const expr_node_t *node)
{
    const char *cidr_str = NULL;
    const char *ip_str = NULL;
    char *ip_dup = NULL;  /* non-NULL when we own the ip_str memory */

    if (node->right) {
        /* Binary form: left -ipmatch right */
        const char *ip_raw = eval_node_str(session, node->left);
        ip_dup = ip_raw ? strdup(ip_raw) : NULL;
        cidr_str = eval_node_str(session, node->right);
        ip_str = ip_dup;
    } else {
        /* Unary form: -ipmatch cidr (use REMOTE_ADDR) */
        cidr_str = eval_node_str(session, node->left);
        if (session) {
            int len = 0;
            ip_str = lsi_session_get_client_ip(session, &len);
        }
    }

    if (!cidr_str || !cidr_str[0] || !ip_str || !ip_str[0]) {
        free(ip_dup);
        return 0;
    }

    int result = 0;

    /* Try IPv6 first, then IPv4 */
    cidr_v6_t cidr6;
    if (cidr_v6_parse(cidr_str, &cidr6) == 0) {
        uint8_t addr6[16];
        if (ip_parse_v6(ip_str, addr6) == 0) {
            result = cidr_v6_match(&cidr6, addr6);
            free(ip_dup);
            return result;
        }
    }

    cidr_v4_t cidr4;
    if (cidr_parse(cidr_str, &cidr4) == 0) {
        uint32_t ip;
        if (ip_parse(ip_str, &ip) == 0) {
            result = cidr_match(&cidr4, ip);
            free(ip_dup);
            return result;
        }
    }

    free(ip_dup);
    return 0;
}

int expr_eval(lsi_session_t *session, const expr_node_t *node)
{
    if (!node)
        return 0;

    switch (node->type) {

    /* --- Boolean operators with short-circuit --- */
    case NODE_AND: {
        int left = expr_eval(session, node->left);
        if (!left) return 0;  /* Short-circuit: left is false */
        return expr_eval(session, node->right) ? 1 : 0;
    }
    case NODE_OR: {
        int left = expr_eval(session, node->left);
        if (left) return 1;  /* Short-circuit: left is true */
        return expr_eval(session, node->right) ? 1 : 0;
    }
    case NODE_NOT:
        return expr_eval(session, node->left) ? 0 : 1;

    /* --- File tests --- */
    case NODE_FILE_F:
    case NODE_FILE_D:
    case NODE_FILE_S:
    case NODE_FILE_L:
    case NODE_FILE_E:
        return eval_file_test(session, node->type, node->left);

    /* --- IP match --- */
    case NODE_IPMATCH:
        return eval_ipmatch(session, node);

    /* --- String comparisons --- */
    /* Note: eval_node_str() may return a static TLS buffer, so we must
       strdup the left value before evaluating the right to avoid aliasing. */
    case NODE_CMP_EQ: {
        const char *l_raw = eval_node_str(session, node->left);
        char *l = l_raw ? strdup(l_raw) : NULL;
        const char *r = eval_node_str(session, node->right);
        int result = (l && r) ? (strcmp(l, r) == 0) : (l == NULL && (r == NULL || r[0] == '\0'));
        free(l);
        return result ? 1 : 0;
    }
    case NODE_CMP_NE: {
        const char *l_raw = eval_node_str(session, node->left);
        char *l = l_raw ? strdup(l_raw) : NULL;
        const char *r = eval_node_str(session, node->right);
        int result = (l && r) ? (strcmp(l, r) != 0) : !(l == NULL && (r == NULL || r[0] == '\0'));
        free(l);
        return result ? 1 : 0;
    }
    case NODE_CMP_LT: {
        const char *l_raw = eval_node_str(session, node->left);
        char *l = l_raw ? strdup(l_raw) : NULL;
        const char *r = eval_node_str(session, node->right);
        int result = (l && r) ? (strcmp(l, r) < 0) : 0;
        free(l);
        return result ? 1 : 0;
    }
    case NODE_CMP_GT: {
        const char *l_raw = eval_node_str(session, node->left);
        char *l = l_raw ? strdup(l_raw) : NULL;
        const char *r = eval_node_str(session, node->right);
        int result = (l && r) ? (strcmp(l, r) > 0) : 0;
        free(l);
        return result ? 1 : 0;
    }
    case NODE_CMP_LE: {
        const char *l_raw = eval_node_str(session, node->left);
        char *l = l_raw ? strdup(l_raw) : NULL;
        const char *r = eval_node_str(session, node->right);
        int result = (l && r) ? (strcmp(l, r) <= 0) : 0;
        free(l);
        return result ? 1 : 0;
    }
    case NODE_CMP_GE: {
        const char *l_raw = eval_node_str(session, node->left);
        char *l = l_raw ? strdup(l_raw) : NULL;
        const char *r = eval_node_str(session, node->right);
        int result = (l && r) ? (strcmp(l, r) >= 0) : 0;
        free(l);
        return result ? 1 : 0;
    }

    /* --- Regex comparisons --- */
    case NODE_REGEX_MATCH: {
        const char *l_raw = eval_node_str(session, node->left);
        char *l = l_raw ? strdup(l_raw) : NULL;
        const char *r = eval_node_str(session, node->right);
        int result = (l && r) ? eval_regex_match(l, r, 0) : 0;
        free(l);
        return result;
    }
    case NODE_REGEX_NOMATCH: {
        const char *l_raw = eval_node_str(session, node->left);
        char *l = l_raw ? strdup(l_raw) : NULL;
        const char *r = eval_node_str(session, node->right);
        int result = (l && r) ? eval_regex_match(l, r, 1) : 0;
        free(l);
        return result;
    }

    /* --- Integer comparisons --- */
    case NODE_INT_EQ:
    case NODE_INT_NE:
    case NODE_INT_LT:
    case NODE_INT_LE:
    case NODE_INT_GT:
    case NODE_INT_GE: {
        const char *l_raw = eval_node_str(session, node->left);
        char *ls = l_raw ? strdup(l_raw) : NULL;
        const char *rs = eval_node_str(session, node->right);
        char *endp;
        errno = 0;
        long lv = strtol(ls ? ls : "", &endp, 10);
        if (errno || *endp != '\0') { free(ls); return 0; }
        errno = 0;
        long rv = strtol(rs ? rs : "", &endp, 10);
        if (errno || *endp != '\0') { free(ls); return 0; }
        free(ls);
        switch (node->type) {
        case NODE_INT_EQ: return (lv == rv) ? 1 : 0;
        case NODE_INT_NE: return (lv != rv) ? 1 : 0;
        case NODE_INT_LT: return (lv <  rv) ? 1 : 0;
        case NODE_INT_LE: return (lv <= rv) ? 1 : 0;
        case NODE_INT_GT: return (lv >  rv) ? 1 : 0;
        case NODE_INT_GE: return (lv >= rv) ? 1 : 0;
        default: return 0;
        }
    }

    /* --- Leaf nodes: truthiness check --- */
    case NODE_STRING:
    case NODE_INTEGER:
        return (node->str_val && node->str_val[0] &&
                strcmp(node->str_val, "0") != 0 &&
                strcmp(node->str_val, "false") != 0) ? 1 : 0;

    case NODE_VAR: {
        const char *val = expr_expand_var(session, node->str_val);
        return (val && val[0]) ? 1 : 0;
    }

    /* --- Function calls return truthiness of result --- */
    case NODE_FUNC_TOLOWER:
    case NODE_FUNC_TOUPPER: {
        const char *val = eval_node_str(session, node);
        return (val && val[0]) ? 1 : 0;
    }

    default:
        return 0;
    }
}

/* ================================================================== */
/*  Deep copy                                                          */
/* ================================================================== */

expr_node_t *expr_clone(const expr_node_t *node)
{
    if (!node)
        return NULL;

    expr_node_t *n = calloc(1, sizeof(expr_node_t));
    if (!n) return NULL;

    n->type = node->type;
    if (node->str_val) {
        n->str_val = strdup(node->str_val);
        if (!n->str_val) { free(n); return NULL; }
    }
    if (node->left) {
        n->left = expr_clone(node->left);
        if (!n->left) { free(n->str_val); free(n); return NULL; }
    }
    if (node->right) {
        n->right = expr_clone(node->right);
        if (!n->right) { expr_free(n); return NULL; }
    }

    return n;
}

/* ================================================================== */
/*  AST -> text printer                                                */
/* ================================================================== */

/**
 * Internal: append AST node as text to a dynamically growing buffer.
 * Returns 0 on success, -1 on error.
 */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} expr_strbuf_t;

static int esb_ensure(expr_strbuf_t *sb, size_t extra)
{
    size_t needed = sb->len + extra + 1;
    if (needed <= sb->cap) return 0;
    size_t nc = sb->cap * 2;
    if (nc < needed) nc = needed;
    char *tmp = realloc(sb->buf, nc);
    if (!tmp) return -1;
    sb->buf = tmp;
    sb->cap = nc;
    return 0;
}

static int esb_append(expr_strbuf_t *sb, const char *s)
{
    size_t slen = strlen(s);
    if (esb_ensure(sb, slen) != 0) return -1;
    memcpy(sb->buf + sb->len, s, slen);
    sb->len += slen;
    sb->buf[sb->len] = '\0';
    return 0;
}

/**
 * Check if a node needs parentheses when it's a child of a parent with
 * given precedence. Returns 1 if parens needed.
 */
static int needs_parens(const expr_node_t *node, const expr_node_t *parent)
{
    if (!node || !parent) return 0;

    /* AND child of OR needs parens */
    if (parent->type == NODE_OR && node->type == NODE_AND) return 0; /* actually AND binds tighter */
    /* OR child of AND needs parens */
    if (parent->type == NODE_AND && node->type == NODE_OR) return 1;

    return 0;
}

static int expr_to_string_r(expr_strbuf_t *sb, const expr_node_t *node, const expr_node_t *parent)
{
    if (!node) return 0;

    switch (node->type) {
    case NODE_STRING:
        if (esb_append(sb, "'") != 0) return -1;
        if (node->str_val && esb_append(sb, node->str_val) != 0) return -1;
        if (esb_append(sb, "'") != 0) return -1;
        return 0;

    case NODE_VAR:
        if (node->str_val && esb_append(sb, node->str_val) != 0) return -1;
        return 0;

    case NODE_REGEX:
        if (esb_append(sb, "/") != 0) return -1;
        if (node->str_val && esb_append(sb, node->str_val) != 0) return -1;
        if (esb_append(sb, "/") != 0) return -1;
        return 0;

    case NODE_INTEGER:
        if (node->str_val && esb_append(sb, node->str_val) != 0) return -1;
        return 0;

    case NODE_NOT:
        if (esb_append(sb, "!") != 0) return -1;
        /* If child is a complex expression, add parens */
        if (node->left && (node->left->type == NODE_AND ||
                           node->left->type == NODE_OR ||
                           (node->left->type >= NODE_CMP_EQ && node->left->type <= NODE_INT_GE))) {
            if (esb_append(sb, "(") != 0) return -1;
            if (expr_to_string_r(sb, node->left, node) != 0) return -1;
            if (esb_append(sb, ")") != 0) return -1;
        } else {
            if (expr_to_string_r(sb, node->left, node) != 0) return -1;
        }
        return 0;

    case NODE_AND:
    case NODE_OR: {
        int paren = needs_parens(node, parent);
        if (paren && esb_append(sb, "(") != 0) return -1;
        if (expr_to_string_r(sb, node->left, node) != 0) return -1;
        if (esb_append(sb, (node->type == NODE_AND) ? " && " : " || ") != 0) return -1;
        if (expr_to_string_r(sb, node->right, node) != 0) return -1;
        if (paren && esb_append(sb, ")") != 0) return -1;
        return 0;
    }

    /* Binary comparisons */
    case NODE_CMP_EQ: case NODE_CMP_NE: case NODE_CMP_LT: case NODE_CMP_GT:
    case NODE_CMP_LE: case NODE_CMP_GE: case NODE_REGEX_MATCH: case NODE_REGEX_NOMATCH:
    case NODE_INT_EQ: case NODE_INT_NE: case NODE_INT_LT: case NODE_INT_LE:
    case NODE_INT_GT: case NODE_INT_GE: {
        if (expr_to_string_r(sb, node->left, node) != 0) return -1;
        const char *op = "";
        switch (node->type) {
        case NODE_CMP_EQ:        op = " == "; break;
        case NODE_CMP_NE:        op = " != "; break;
        case NODE_CMP_LT:        op = " < ";  break;
        case NODE_CMP_GT:        op = " > ";  break;
        case NODE_CMP_LE:        op = " <= "; break;
        case NODE_CMP_GE:        op = " >= "; break;
        case NODE_REGEX_MATCH:   op = " =~ "; break;
        case NODE_REGEX_NOMATCH: op = " !~ "; break;
        case NODE_INT_EQ:        op = " -eq "; break;
        case NODE_INT_NE:        op = " -ne "; break;
        case NODE_INT_LT:        op = " -lt "; break;
        case NODE_INT_LE:        op = " -le "; break;
        case NODE_INT_GT:        op = " -gt "; break;
        case NODE_INT_GE:        op = " -ge "; break;
        default: break;
        }
        if (esb_append(sb, op) != 0) return -1;
        if (expr_to_string_r(sb, node->right, node) != 0) return -1;
        return 0;
    }

    /* File tests */
    case NODE_FILE_F: if (esb_append(sb, "-f ") != 0) return -1;
        return expr_to_string_r(sb, node->left, node);
    case NODE_FILE_D: if (esb_append(sb, "-d ") != 0) return -1;
        return expr_to_string_r(sb, node->left, node);
    case NODE_FILE_S: if (esb_append(sb, "-s ") != 0) return -1;
        return expr_to_string_r(sb, node->left, node);
    case NODE_FILE_L: if (esb_append(sb, "-l ") != 0) return -1;
        return expr_to_string_r(sb, node->left, node);
    case NODE_FILE_E: if (esb_append(sb, "-e ") != 0) return -1;
        return expr_to_string_r(sb, node->left, node);

    /* IP match */
    case NODE_IPMATCH:
        if (node->right) {
            /* Binary: left -ipmatch right */
            if (expr_to_string_r(sb, node->left, node) != 0) return -1;
            if (esb_append(sb, " -ipmatch ") != 0) return -1;
            return expr_to_string_r(sb, node->right, node);
        }
        if (esb_append(sb, "-ipmatch ") != 0) return -1;
        return expr_to_string_r(sb, node->left, node);

    /* Function calls */
    case NODE_FUNC_TOLOWER:
        if (esb_append(sb, "tolower(") != 0) return -1;
        if (expr_to_string_r(sb, node->left, node) != 0) return -1;
        return esb_append(sb, ")");

    case NODE_FUNC_TOUPPER:
        if (esb_append(sb, "toupper(") != 0) return -1;
        if (expr_to_string_r(sb, node->left, node) != 0) return -1;
        return esb_append(sb, ")");

    default:
        return 0;
    }
}

char *expr_to_string(const expr_node_t *node)
{
    if (!node) return NULL;

    expr_strbuf_t sb;
    sb.cap = 256;
    sb.len = 0;
    sb.buf = malloc(sb.cap);
    if (!sb.buf) return NULL;
    sb.buf[0] = '\0';

    if (expr_to_string_r(&sb, node, NULL) != 0) {
        free(sb.buf);
        return NULL;
    }

    return sb.buf;
}

/* ================================================================== */
/*  Free                                                               */
/* ================================================================== */

void expr_free(expr_node_t *node)
{
    if (!node) return;
    expr_free(node->left);
    expr_free(node->right);
    free(node->str_val);
    free(node);
}
