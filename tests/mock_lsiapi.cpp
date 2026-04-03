/**
 * mock_lsiapi.cpp - LSIAPI Mock Layer Implementation
 *
 * Provides in-memory simulation of LSIAPI interfaces for testing.
 * All state is stored in MockSession objects and global registries.
 *
 * Validates: Requirements 1.1, 1.3
 */
#include "mock_lsiapi.h"

#include <cstdarg>
#include <cstdio>
#include <algorithm>

/* Provide the g_api symbol required by htaccess_exec_rewrite.c.
 * Set to NULL — rewrite exec detects this and returns -1 (unsupported).
 * Type is void* because mock_lsiapi.h blocks ls.h (which defines lsi_api_t).
 * This is safe: g_api is only checked for NULL in test mode, never dereferenced. */
__thread const void *g_api = NULL;

/* ================================================================== */
/*  Internal helpers                                                    */
/* ================================================================== */

static inline MockSession *to_mock(lsi_session_t *s) {
    return reinterpret_cast<MockSession *>(s);
}

static inline std::string make_str(const char *p, int len) {
    if (!p || len <= 0) return {};
    return std::string(p, static_cast<size_t>(len));
}

/* ================================================================== */
/*  Global registries                                                   */
/* ================================================================== */

static std::vector<HookRecord> g_hook_records;
static std::vector<LogRecord>  g_log_records;

namespace mock_lsiapi {

const std::vector<HookRecord> &get_hook_records() { return g_hook_records; }
const std::vector<LogRecord>  &get_log_records()  { return g_log_records; }

void reset_global_state() {
    g_hook_records.clear();
    g_log_records.clear();
}

} /* namespace mock_lsiapi */

/* ================================================================== */
/*  MockSession implementation                                          */
/* ================================================================== */

MockSession::MockSession() : status_code_(200) {}
MockSession::~MockSession() = default;

void MockSession::set_request_uri(const std::string &uri)  { request_uri_ = uri; }
void MockSession::set_doc_root(const std::string &root)    { doc_root_ = root; }
void MockSession::set_client_ip(const std::string &ip)     { client_ip_ = ip; }
void MockSession::set_status_code(int code)                { status_code_ = code; }

void MockSession::add_request_header(const std::string &name, const std::string &value) {
    req_headers_[name] = value;
}

void MockSession::add_response_header(const std::string &name, const std::string &value) {
    resp_headers_[name].push_back(value);
}

void MockSession::add_env_var(const std::string &name, const std::string &value) {
    env_vars_[name] = value;
}

std::string MockSession::get_request_uri() const { return request_uri_; }
std::string MockSession::get_doc_root() const    { return doc_root_; }
std::string MockSession::get_client_ip() const   { return client_ip_; }
int         MockSession::get_status_code() const { return status_code_; }

bool MockSession::has_request_header(const std::string &name) const {
    return req_headers_.count(name) > 0;
}

std::string MockSession::get_request_header(const std::string &name) const {
    auto it = req_headers_.find(name);
    return (it != req_headers_.end()) ? it->second : std::string();
}

bool MockSession::has_response_header(const std::string &name) const {
    auto it = resp_headers_.find(name);
    return it != resp_headers_.end() && !it->second.empty();
}

std::string MockSession::get_response_header(const std::string &name) const {
    auto it = resp_headers_.find(name);
    if (it == resp_headers_.end() || it->second.empty()) return {};
    return it->second.front();
}

std::vector<std::string> MockSession::get_all_response_headers(const std::string &name) const {
    auto it = resp_headers_.find(name);
    if (it == resp_headers_.end()) return {};
    return it->second;
}

int MockSession::count_response_headers(const std::string &name) const {
    auto it = resp_headers_.find(name);
    if (it == resp_headers_.end()) return 0;
    return static_cast<int>(it->second.size());
}

bool MockSession::has_env_var(const std::string &name) const {
    return env_vars_.count(name) > 0;
}

std::string MockSession::get_env_var(const std::string &name) const {
    auto it = env_vars_.find(name);
    return (it != env_vars_.end()) ? it->second : std::string();
}

const std::vector<PhpIniRecord> &MockSession::get_php_ini_records() const {
    return php_ini_records_;
}

std::string MockSession::get_resp_body() const { return resp_body_; }

/* v2 setup helpers */
void MockSession::set_method(const std::string &method)       { method_ = method; }
void MockSession::set_auth_header(const std::string &value)   { auth_header_ = value; }
void MockSession::add_existing_file(const std::string &path)  { existing_files_[path] = true; }
void MockSession::set_query_string(const std::string &qs)     { query_string_ = qs; }

/* v2 inspection helpers */
std::string MockSession::get_method() const              { return method_; }
std::string MockSession::get_query_string() const        { return query_string_; }
std::string MockSession::get_auth_header_value() const   { return auth_header_; }
std::string MockSession::get_internal_uri() const        { return internal_uri_; }
std::string MockSession::get_www_authenticate() const    { return www_authenticate_; }

int MockSession::get_dir_option(const std::string &option) const {
    auto it = dir_options_.find(option);
    return (it != dir_options_.end()) ? it->second : -1; /* -1 = not set */
}

bool MockSession::file_exists(const std::string &path) const {
    return existing_files_.count(path) > 0;
}

void MockSession::reset() {
    req_headers_.clear();
    resp_headers_.clear();
    env_vars_.clear();
    request_uri_.clear();
    doc_root_.clear();
    client_ip_.clear();
    status_code_ = 200;
    php_ini_records_.clear();
    resp_body_.clear();
    dir_options_.clear();
    internal_uri_.clear();
    method_.clear();
    query_string_.clear();
    auth_header_.clear();
    www_authenticate_.clear();
    existing_files_.clear();
}

lsi_session_t *MockSession::handle() {
    return reinterpret_cast<lsi_session_t *>(this);
}


/* ================================================================== */
/*  C-linkage LSIAPI stub implementations                              */
/* ================================================================== */

extern "C" {

/* ---- Request headers ---- */

const char *lsi_session_get_req_header_by_name(lsi_session_t *session,
                                                const char *name, int name_len,
                                                int *val_len) {
    auto *m = to_mock(session);
    std::string key = make_str(name, name_len);
    auto it = m->req_headers_.find(key);
    if (it == m->req_headers_.end()) {
        if (val_len) *val_len = 0;
        return nullptr;
    }
    if (val_len) *val_len = static_cast<int>(it->second.size());
    return it->second.c_str();
}

int lsi_session_set_req_header(lsi_session_t *session,
                                const char *name, int name_len,
                                const char *val, int val_len) {
    auto *m = to_mock(session);
    m->req_headers_[make_str(name, name_len)] = make_str(val, val_len);
    return LSI_OK;
}

int lsi_session_remove_req_header(lsi_session_t *session,
                                   const char *name, int name_len) {
    auto *m = to_mock(session);
    m->req_headers_.erase(make_str(name, name_len));
    return LSI_OK;
}

/* ---- Response headers ---- */

const char *lsi_session_get_resp_header_by_name(lsi_session_t *session,
                                                 const char *name, int name_len,
                                                 int *val_len) {
    auto *m = to_mock(session);
    std::string key = make_str(name, name_len);
    auto it = m->resp_headers_.find(key);
    if (it == m->resp_headers_.end() || it->second.empty()) {
        if (val_len) *val_len = 0;
        return nullptr;
    }
    /* Return the first value (for single-value semantics) */
    const std::string &v = it->second.front();
    if (val_len) *val_len = static_cast<int>(v.size());
    return v.c_str();
}

int lsi_session_set_resp_header(lsi_session_t *session,
                                 const char *name, int name_len,
                                 const char *val, int val_len) {
    auto *m = to_mock(session);
    std::string key = make_str(name, name_len);
    /* "set" replaces all existing values with a single value */
    m->resp_headers_[key].clear();
    m->resp_headers_[key].push_back(make_str(val, val_len));
    return LSI_OK;
}

int lsi_session_set_resp_content_type(lsi_session_t *session,
                                       const char *val, int val_len) {
    /* Mock: delegates to name-based set. In production, this uses
     * LSI_RSPHDR_CONTENT_TYPE (indexed) + remove_resp_header to replace
     * OLS engine-set Content-Type. The mock uses a single header store
     * so name-based set correctly replaces existing values. */
    return lsi_session_set_resp_header(session, "Content-Type", 12, val, val_len);
}

const char *lsi_session_get_resp_content_type(lsi_session_t *session,
                                               int *val_len) {
    /* Mock: delegates to name-based get. In production, this uses
     * LSI_RSPHDR_CONTENT_TYPE (indexed) with fallback to name-based. */
    return lsi_session_get_resp_header_by_name(session, "Content-Type", 12, val_len);
}

int lsi_session_add_resp_header(lsi_session_t *session,
                                 const char *name, int name_len,
                                 const char *val, int val_len) {
    auto *m = to_mock(session);
    /* "add" appends a new header entry (even if name already exists) */
    m->resp_headers_[make_str(name, name_len)].push_back(make_str(val, val_len));
    return LSI_OK;
}

int lsi_session_append_resp_header(lsi_session_t *session,
                                    const char *name, int name_len,
                                    const char *val, int val_len) {
    auto *m = to_mock(session);
    std::string key = make_str(name, name_len);
    auto &vec = m->resp_headers_[key];
    std::string append_val = make_str(val, val_len);
    if (vec.empty()) {
        vec.push_back(append_val);
    } else {
        /* Append to the first value, comma-separated */
        vec.front() += ", " + append_val;
    }
    return LSI_OK;
}

int lsi_session_remove_resp_header(lsi_session_t *session,
                                    const char *name, int name_len) {
    auto *m = to_mock(session);
    m->resp_headers_.erase(make_str(name, name_len));
    return LSI_OK;
}

int lsi_session_get_resp_header_count(lsi_session_t *session,
                                       const char *name, int name_len) {
    auto *m = to_mock(session);
    std::string key = make_str(name, name_len);
    auto it = m->resp_headers_.find(key);
    if (it == m->resp_headers_.end()) return 0;
    return static_cast<int>(it->second.size());
}

/* ---- Environment variables ---- */

const char *lsi_session_get_env(lsi_session_t *session,
                                 const char *name, int name_len,
                                 int *val_len) {
    auto *m = to_mock(session);
    std::string key = make_str(name, name_len);
    auto it = m->env_vars_.find(key);
    if (it == m->env_vars_.end()) {
        if (val_len) *val_len = 0;
        return nullptr;
    }
    if (val_len) *val_len = static_cast<int>(it->second.size());
    return it->second.c_str();
}

int lsi_session_set_env(lsi_session_t *session,
                         const char *name, int name_len,
                         const char *val, int val_len) {
    auto *m = to_mock(session);
    m->env_vars_[make_str(name, name_len)] = make_str(val, val_len);
    return LSI_OK;
}

/* ---- Response status ---- */

int lsi_session_get_status(lsi_session_t *session) {
    return to_mock(session)->status_code_;
}

int lsi_session_set_status(lsi_session_t *session, int code) {
    to_mock(session)->status_code_ = code;
    return LSI_OK;
}

void lsi_session_end_resp(lsi_session_t *session) {
    /* No-op in mock — just marks the response as ended */
    (void)session;
}

/* ---- Request URI ---- */

const char *lsi_session_get_uri(lsi_session_t *session, int *uri_len) {
    auto *m = to_mock(session);
    if (uri_len) *uri_len = static_cast<int>(m->request_uri_.size());
    return m->request_uri_.c_str();
}

/* ---- Document root ---- */

const char *lsi_session_get_doc_root(lsi_session_t *session, int *len) {
    auto *m = to_mock(session);
    if (len) *len = static_cast<int>(m->doc_root_.size());
    return m->doc_root_.c_str();
}

/* ---- Client IP ---- */

const char *lsi_session_get_client_ip(lsi_session_t *session, int *len) {
    auto *m = to_mock(session);
    if (len) *len = static_cast<int>(m->client_ip_.size());
    return m->client_ip_.c_str();
}

/* ---- Redirect ---- */

int lsi_session_redirect(lsi_session_t *session,
                          int status_code, const char *url, int url_len) {
    auto *m = to_mock(session);
    m->status_code_ = status_code;
    if (url && url_len > 0) {
        std::string key = "Location";
        m->resp_headers_[key].clear();
        m->resp_headers_[key].push_back(std::string(url, (size_t)url_len));
    }
    return LSI_OK;
}

/* ---- PHP configuration ---- */

int lsi_session_set_php_ini(lsi_session_t *session,
                             const char *name, const char *val,
                             int type) {
    auto *m = to_mock(session);
    PhpIniRecord rec;
    rec.name     = name ? std::string(name) : std::string();
    rec.value    = val ? std::string(val) : std::string();
    rec.type     = type;
    rec.is_admin = (type >= PHP_INI_TYPE_ADMIN_VALUE);
    m->php_ini_records_.push_back(std::move(rec));
    return LSI_OK;
}

/* ---- Response body ---- */

int lsi_session_set_resp_body(lsi_session_t *session,
                               const char *buf, int len) {
    auto *m = to_mock(session);
    m->resp_body_ = make_str(buf, len);
    return LSI_OK;
}

/* ---- Hook registration ---- */

int lsi_register_hook(int hook_point, lsi_hook_cb cb, int priority) {
    HookRecord rec;
    rec.hook_point = hook_point;
    rec.callback   = cb;
    rec.priority   = priority;
    g_hook_records.push_back(rec);
    return LSI_OK;
}

/* ---- Logging ---- */

void lsi_log(lsi_session_t * /*session*/, int level, const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    LogRecord rec;
    rec.level   = level;
    rec.message = buf;
    g_log_records.push_back(std::move(rec));
}

/* ---- v2: Directory options ---- */

int lsi_session_set_dir_option(lsi_session_t *session,
                                const char *option, int enabled) {
    if (!session || !option) return LSI_ERROR;
    auto *m = to_mock(session);
    m->dir_options_[std::string(option)] = enabled;
    return LSI_OK;
}

int lsi_session_get_dir_option(lsi_session_t *session,
                                const char *option) {
    if (!session || !option) return -1;
    auto *m = to_mock(session);
    auto it = m->dir_options_.find(std::string(option));
    return (it != m->dir_options_.end()) ? it->second : -1;
}

/* ---- v2: Internal URI redirect ---- */

int lsi_session_set_uri_internal(lsi_session_t *session,
                                  const char *uri, int uri_len) {
    if (!session || !uri || uri_len <= 0) return LSI_ERROR;
    auto *m = to_mock(session);
    m->internal_uri_ = std::string(uri, static_cast<size_t>(uri_len));
    return LSI_OK;
}

/* ---- v2: File existence check ---- */

int lsi_session_file_exists(lsi_session_t *session, const char *path) {
    if (!session || !path) return 0;
    auto *m = to_mock(session);
    return m->existing_files_.count(std::string(path)) > 0 ? 1 : 0;
}

/* ---- v2: Request method ---- */

const char *lsi_session_get_method(lsi_session_t *session, int *len) {
    if (!session) {
        if (len) *len = 0;
        return nullptr;
    }
    auto *m = to_mock(session);
    if (m->method_.empty()) {
        if (len) *len = 0;
        return nullptr;
    }
    if (len) *len = static_cast<int>(m->method_.size());
    return m->method_.c_str();
}

/* ---- v2: Query string ---- */

const char *lsi_session_get_query_string(lsi_session_t *session, int *len) {
    if (!session) {
        if (len) *len = 0;
        return nullptr;
    }
    auto *m = to_mock(session);
    if (m->query_string_.empty()) {
        if (len) *len = 0;
        return nullptr;
    }
    if (len) *len = static_cast<int>(m->query_string_.size());
    return m->query_string_.c_str();
}

/* ---- v2: Authorization header ---- */

const char *lsi_session_get_auth_header(lsi_session_t *session, int *len) {
    if (!session) {
        if (len) *len = 0;
        return nullptr;
    }
    auto *m = to_mock(session);
    if (m->auth_header_.empty()) {
        if (len) *len = 0;
        return nullptr;
    }
    if (len) *len = static_cast<int>(m->auth_header_.size());
    return m->auth_header_.c_str();
}

/* ---- v2: WWW-Authenticate header ---- */

int lsi_session_set_www_authenticate(lsi_session_t *session,
                                      const char *realm, int realm_len) {
    if (!session || !realm || realm_len <= 0) return LSI_ERROR;
    auto *m = to_mock(session);
    m->www_authenticate_ = "Basic realm=\""
                         + std::string(realm, static_cast<size_t>(realm_len))
                         + "\"";
    return LSI_OK;
}

} /* extern "C" */
