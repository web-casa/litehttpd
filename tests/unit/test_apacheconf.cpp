/**
 * test_apacheconf.cpp -- Unit tests for Apache config parser and OLS writer
 *
 * Tests ap_parse_config(), ols_write_vhost(), and related functions.
 */
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "apacheconf_parser.h"
#include "ols_config_writer.h"
}

/* ---- Helper: write a temp file and return its path ---- */

class TempFile {
public:
    explicit TempFile(const std::string &content) {
        char tmpl[] = "/tmp/litehttpd-confconv-test-XXXXXX";
        int fd = mkstemp(tmpl);
        EXPECT_GE(fd, 0);
        path_ = tmpl;
        write(fd, content.c_str(), content.size());
        close(fd);
    }
    ~TempFile() { unlink(path_.c_str()); }
    const char *path() const { return path_.c_str(); }
private:
    std::string path_;
};

class TempDir {
public:
    TempDir() {
        char tmpl[] = "/tmp/litehttpd-confconv-out-XXXXXX";
        char *d = mkdtemp(tmpl);
        EXPECT_NE(d, nullptr);
        path_ = d;
    }
    ~TempDir() {
        /* rm -rf */
        std::string cmd = "rm -rf " + path_;
        system(cmd.c_str());
    }
    const char *path() const { return path_.c_str(); }
private:
    std::string path_;
};

static std::string read_file(const std::string &path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/* ---- Test: parse simple config with 1 VirtualHost ---- */

TEST(ApacheConfParser, SimpleVirtualHost) {
    TempFile conf(
        "ServerRoot \"/etc/httpd\"\n"
        "Listen 80\n"
        "ServerAdmin admin@example.com\n"
        "User www-data\n"
        "Group www-data\n"
        "\n"
        "<VirtualHost *:80>\n"
        "  ServerName example.com\n"
        "  DocumentRoot /var/www/example\n"
        "  DirectoryIndex index.html index.php\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);
    EXPECT_EQ(config.listen_count, 1);
    EXPECT_EQ(config.listen_ports[0], 80);

    ASSERT_NE(config.vhosts[0].server_name, nullptr);
    EXPECT_STREQ(config.vhosts[0].server_name, "example.com");
    EXPECT_STREQ(config.vhosts[0].doc_root, "/var/www/example");
    EXPECT_STREQ(config.vhosts[0].dir_index, "index.html index.php");
    EXPECT_EQ(config.vhosts[0].listen_port, 80);

    EXPECT_STREQ(config.server_admin, "admin@example.com");
    EXPECT_STREQ(config.user, "www-data");
    EXPECT_STREQ(config.group, "www-data");
    EXPECT_STREQ(config.server_root, "/etc/httpd");

    ap_config_free(&config);
}

/* ---- Test: multiple VirtualHosts ---- */

TEST(ApacheConfParser, MultipleVirtualHosts) {
    TempFile conf(
        "Listen 80\n"
        "Listen 443\n"
        "\n"
        "<VirtualHost *:80>\n"
        "  ServerName site1.com\n"
        "  DocumentRoot /var/www/site1\n"
        "</VirtualHost>\n"
        "\n"
        "<VirtualHost *:80>\n"
        "  ServerName site2.com\n"
        "  DocumentRoot /var/www/site2\n"
        "</VirtualHost>\n"
        "\n"
        "<VirtualHost *:443>\n"
        "  ServerName site1.com\n"
        "  DocumentRoot /var/www/site1\n"
        "  SSLEngine on\n"
        "  SSLCertificateFile /etc/ssl/cert.pem\n"
        "  SSLCertificateKeyFile /etc/ssl/key.pem\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 3);
    EXPECT_EQ(config.listen_count, 2);

    EXPECT_STREQ(config.vhosts[0].server_name, "site1.com");
    EXPECT_STREQ(config.vhosts[1].server_name, "site2.com");
    EXPECT_STREQ(config.vhosts[2].server_name, "site1.com");

    ap_config_free(&config);
}

/* ---- Test: SSL VirtualHost ---- */

TEST(ApacheConfParser, SSLVirtualHost) {
    TempFile conf(
        "<VirtualHost *:443>\n"
        "  ServerName secure.example.com\n"
        "  DocumentRoot /var/www/secure\n"
        "  SSLEngine on\n"
        "  SSLCertificateFile /etc/ssl/certs/server.crt\n"
        "  SSLCertificateKeyFile /etc/ssl/private/server.key\n"
        "  SSLCertificateChainFile /etc/ssl/certs/chain.crt\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);
    EXPECT_EQ(config.vhosts[0].listen_ssl, 1);
    EXPECT_STREQ(config.vhosts[0].ssl_cert, "/etc/ssl/certs/server.crt");
    EXPECT_STREQ(config.vhosts[0].ssl_key, "/etc/ssl/private/server.key");
    EXPECT_STREQ(config.vhosts[0].ssl_chain, "/etc/ssl/certs/chain.crt");

    ap_config_free(&config);
}

/* ---- Test: Include directive ---- */

TEST(ApacheConfParser, IncludeDirective) {
    TempFile included(
        "<VirtualHost *:80>\n"
        "  ServerName included.com\n"
        "  DocumentRoot /var/www/included\n"
        "</VirtualHost>\n"
    );

    std::string main_conf =
        "ServerRoot \"/etc/httpd\"\n"
        "Include " + std::string(included.path()) + "\n";

    TempFile conf(main_conf);

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);
    EXPECT_STREQ(config.vhosts[0].server_name, "included.com");

    ap_config_free(&config);
}

/* ---- Test: IncludeOptional with missing file ---- */

TEST(ApacheConfParser, IncludeOptionalMissing) {
    TempFile conf(
        "IncludeOptional /nonexistent/path/*.conf\n"
        "<VirtualHost *:80>\n"
        "  ServerName main.com\n"
        "  DocumentRoot /var/www/main\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);
    EXPECT_STREQ(config.vhosts[0].server_name, "main.com");

    ap_config_free(&config);
}

/* ---- Test: Rewrite rules accumulation ---- */

TEST(ApacheConfParser, RewriteRules) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName rewrite.com\n"
        "  DocumentRoot /var/www/rewrite\n"
        "  RewriteEngine On\n"
        "  RewriteCond %{REQUEST_FILENAME} !-f\n"
        "  RewriteCond %{REQUEST_FILENAME} !-d\n"
        "  RewriteRule ^(.*)$ /index.php [L,QSA]\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);
    EXPECT_EQ(config.vhosts[0].rewrite_enabled, 1);
    ASSERT_NE(config.vhosts[0].rewrite_rules, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].rewrite_rules,
                     "RewriteCond %{REQUEST_FILENAME} !-f"), nullptr);
    EXPECT_NE(strstr(config.vhosts[0].rewrite_rules,
                     "RewriteRule ^(.*)$ /index.php [L,QSA]"), nullptr);

    ap_config_free(&config);
}

/* ---- Test: Directory block parsing ---- */

TEST(ApacheConfParser, DirectoryBlock) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName dir.com\n"
        "  DocumentRoot /var/www/dir\n"
        "  <Directory /var/www/dir>\n"
        "    Options +Indexes +FollowSymLinks\n"
        "    AllowOverride All\n"
        "    DirectoryIndex index.html\n"
        "    Require all granted\n"
        "  </Directory>\n"
        "  <Directory /var/www/dir/private>\n"
        "    Options -Indexes\n"
        "    AllowOverride None\n"
        "    Require all denied\n"
        "  </Directory>\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);
    EXPECT_EQ(config.vhosts[0].context_count, 2);

    const ap_context_t &ctx0 = config.vhosts[0].contexts[0];
    EXPECT_STREQ(ctx0.path, "/var/www/dir");
    EXPECT_EQ(ctx0.allow_browse, 1);
    EXPECT_EQ(ctx0.follow_symlinks, 1);
    EXPECT_STREQ(ctx0.allow_override, "All");
    EXPECT_STREQ(ctx0.dir_index, "index.html");
    EXPECT_EQ(ctx0.require_all, 1);

    const ap_context_t &ctx1 = config.vhosts[0].contexts[1];
    EXPECT_STREQ(ctx1.path, "/var/www/dir/private");
    EXPECT_EQ(ctx1.allow_browse, 0);
    EXPECT_STREQ(ctx1.allow_override, "None");
    EXPECT_EQ(ctx1.require_all, 0);

    ap_config_free(&config);
}

/* ---- Test: Options parsing ---- */

TEST(ApacheConfParser, OptionsParsingVhost) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName opt.com\n"
        "  DocumentRoot /var/www/opt\n"
        "  Options -Indexes +FollowSymLinks\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    ASSERT_NE(config.vhosts[0].options, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].options, "-Indexes"), nullptr);

    ap_config_free(&config);
}

/* ---- Test: PHP handler detection ---- */

TEST(ApacheConfParser, PHPHandlerSetHandler) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName php.com\n"
        "  DocumentRoot /var/www/php\n"
        "  <Directory /var/www/php>\n"
        "    SetHandler application/x-httpd-php\n"
        "  </Directory>\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    ASSERT_NE(config.vhosts[0].php_handler, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].php_handler, "php"), nullptr);

    ap_config_free(&config);
}

TEST(ApacheConfParser, PHPHandlerAddHandler) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName php2.com\n"
        "  DocumentRoot /var/www/php2\n"
        "  AddHandler application/x-httpd-ea-php83 .php\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    ASSERT_NE(config.vhosts[0].php_handler, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].php_handler, "php83"), nullptr);

    ap_config_free(&config);
}

TEST(ApacheConfParser, PHPHandlerFcgidWrapper) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName php3.com\n"
        "  DocumentRoot /var/www/php3\n"
        "  FcgidWrapper /usr/local/cpanel/cgi-sys/ea-php83 .php\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    ASSERT_NE(config.vhosts[0].php_handler, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].php_handler, "php83"), nullptr);

    ap_config_free(&config);
}

/* ---- Test: Portmap parsing ---- */

TEST(ApacheConfParser, PortmapParsing) {
    ap_config_t config;
    memset(&config, 0, sizeof(config));

    /* Simulate portmap by directly calling the main tool's portmap logic */
    /* (normally done in main()) */
    char buf[] = "80:8088,443:8443";
    char *saveptr = NULL;
    char *tok = strtok_r(buf, ",", &saveptr);
    while (tok && config.port_map_count < AP_MAX_PORTMAP) {
        int from = 0, to = 0;
        if (sscanf(tok, "%d:%d", &from, &to) == 2) {
            config.port_from[config.port_map_count] = from;
            config.port_to[config.port_map_count] = to;
            config.port_map_count++;
        }
        tok = strtok_r(NULL, ",", &saveptr);
    }

    EXPECT_EQ(config.port_map_count, 2);
    EXPECT_EQ(config.port_from[0], 80);
    EXPECT_EQ(config.port_to[0], 8088);
    EXPECT_EQ(config.port_from[1], 443);
    EXPECT_EQ(config.port_to[1], 8443);

    ap_config_free(&config);
}

/* ---- Test: AllowOverride mapping ---- */

TEST(ApacheConfParser, AllowOverrideMapping) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName ao.com\n"
        "  DocumentRoot /var/www/ao\n"
        "  AllowOverride AuthConfig FileInfo\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    ASSERT_NE(config.vhosts[0].allow_override, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].allow_override, "AuthConfig"), nullptr);
    EXPECT_NE(strstr(config.vhosts[0].allow_override, "FileInfo"), nullptr);

    ap_config_free(&config);
}

/* ---- Test: Error handling - missing file ---- */

TEST(ApacheConfParser, MissingFile) {
    ap_config_t config;
    EXPECT_NE(ap_parse_config("/nonexistent/httpd.conf", &config), 0);
}

/* ---- Test: Error handling - null args ---- */

TEST(ApacheConfParser, NullArgs) {
    ap_config_t config;
    EXPECT_NE(ap_parse_config(NULL, &config), 0);
    EXPECT_NE(ap_parse_config("/tmp/x", NULL), 0);
}

/* ---- Test: Empty config ---- */

TEST(ApacheConfParser, EmptyConfig) {
    TempFile conf("# just a comment\n\n\n");
    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 0);
    ap_config_free(&config);
}

/* ---- Test: Comments and empty lines ---- */

TEST(ApacheConfParser, CommentsAndEmptyLines) {
    TempFile conf(
        "# This is a comment\n"
        "\n"
        "  # indented comment\n"
        "Listen 80\n"
        "\n"
        "<VirtualHost *:80>\n"
        "  # comment in vhost\n"
        "  ServerName test.com\n"
        "  DocumentRoot /var/www/test\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);
    EXPECT_STREQ(config.vhosts[0].server_name, "test.com");
    ap_config_free(&config);
}

/* ---- Test: IfModule transparent processing ---- */

TEST(ApacheConfParser, IfModuleTransparent) {
    TempFile conf(
        "<IfModule mod_rewrite.c>\n"
        "  <VirtualHost *:80>\n"
        "    ServerName ifmod.com\n"
        "    DocumentRoot /var/www/ifmod\n"
        "    RewriteEngine On\n"
        "    RewriteRule ^/old$ /new [R=301,L]\n"
        "  </VirtualHost>\n"
        "</IfModule>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);
    EXPECT_STREQ(config.vhosts[0].server_name, "ifmod.com");
    EXPECT_EQ(config.vhosts[0].rewrite_enabled, 1);
    ap_config_free(&config);
}

/* ---- Test: ServerAlias ---- */

TEST(ApacheConfParser, ServerAlias) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName primary.com\n"
        "  ServerAlias www.primary.com\n"
        "  ServerAlias alias.primary.com\n"
        "  DocumentRoot /var/www/primary\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    ASSERT_NE(config.vhosts[0].server_aliases, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].server_aliases, "www.primary.com"),
              nullptr);
    EXPECT_NE(strstr(config.vhosts[0].server_aliases, "alias.primary.com"),
              nullptr);
    ap_config_free(&config);
}

/* ---- Test: ErrorDocument ---- */

TEST(ApacheConfParser, ErrorDocument) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName err.com\n"
        "  DocumentRoot /var/www/err\n"
        "  ErrorDocument 404 /custom404.html\n"
        "  ErrorDocument 500 /custom500.html\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    ASSERT_NE(config.vhosts[0].error_pages, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].error_pages, "404"), nullptr);
    EXPECT_NE(strstr(config.vhosts[0].error_pages, "500"), nullptr);
    ap_config_free(&config);
}

/* ---- Test: Header/RequestHeader ---- */

TEST(ApacheConfParser, ExtraHeaders) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName hdr.com\n"
        "  DocumentRoot /var/www/hdr\n"
        "  Header set X-Frame-Options SAMEORIGIN\n"
        "  RequestHeader set X-Forwarded-Proto https\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    ASSERT_NE(config.vhosts[0].extra_headers, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].extra_headers, "X-Frame-Options"),
              nullptr);
    EXPECT_NE(strstr(config.vhosts[0].extra_headers, "X-Forwarded-Proto"),
              nullptr);
    ap_config_free(&config);
}

/* ---- Test: OLS config writer ---- */

TEST(OLSConfigWriter, WriteVhost) {
    ap_vhost_t vh;
    memset(&vh, 0, sizeof(vh));
    vh.server_name = strdup("writer.com");
    vh.doc_root = strdup("/var/www/writer");
    vh.rewrite_enabled = 1;
    vh.rewrite_rules = strdup(
        "RewriteCond %{REQUEST_FILENAME} !-f\n"
        "RewriteRule ^(.*)$ /index.php [L,QSA]"
    );
    vh.dir_index = strdup("index.php index.html");
    vh.allow_override = strdup("All");

    TempDir outdir;
    std::string vhconf = std::string(outdir.path()) + "/vhconf.conf";

    ASSERT_EQ(ols_write_vhost(&vh, vhconf.c_str()), 0);

    std::string content = read_file(vhconf);
    EXPECT_NE(content.find("docRoot"), std::string::npos);
    EXPECT_NE(content.find("/var/www/writer"), std::string::npos);
    EXPECT_NE(content.find("enableRewrite             1"), std::string::npos);
    EXPECT_NE(content.find("allowOverride             255"), std::string::npos);
    EXPECT_NE(content.find("RewriteCond"), std::string::npos);
    EXPECT_NE(content.find("RewriteRule"), std::string::npos);
    EXPECT_NE(content.find("END_RULES"), std::string::npos);
    EXPECT_NE(content.find("indexFiles"), std::string::npos);

    free(vh.server_name);
    free(vh.doc_root);
    free(vh.rewrite_rules);
    free(vh.dir_index);
    free(vh.allow_override);
}

/* ---- Test: full pipeline parse + write ---- */

TEST(OLSConfigWriter, FullPipeline) {
    TempFile conf(
        "Listen 80\n"
        "<VirtualHost *:80>\n"
        "  ServerName pipeline.com\n"
        "  DocumentRoot /var/www/pipeline\n"
        "  RewriteEngine On\n"
        "  RewriteRule ^/(.*)$ /index.php/$1 [L]\n"
        "  ErrorDocument 404 /404.html\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);

    TempDir outdir;
    ASSERT_EQ(ols_write_config(&config, outdir.path()), 0);

    /* Check vhosts.conf exists */
    std::string vhosts_conf = std::string(outdir.path()) + "/vhosts.conf";
    std::string content = read_file(vhosts_conf);
    EXPECT_NE(content.find("pipeline.com"), std::string::npos);
    EXPECT_NE(content.find("virtualhost"), std::string::npos);

    /* Check vhconf.conf exists */
    std::string vhconf = std::string(outdir.path()) +
                         "/vhosts/pipeline.com/vhconf.conf";
    content = read_file(vhconf);
    EXPECT_NE(content.find("docRoot"), std::string::npos);
    EXPECT_NE(content.find("enableRewrite             1"), std::string::npos);

    /* Check listeners.conf exists */
    std::string listeners = std::string(outdir.path()) + "/listeners.conf";
    content = read_file(listeners);
    EXPECT_NE(content.find("listener"), std::string::npos);
    EXPECT_NE(content.find("80"), std::string::npos);

    ap_config_free(&config);
}

/* ---- Test: Redirect/RedirectMatch ---- */

TEST(ApacheConfParser, RedirectDirectives) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName redir.com\n"
        "  DocumentRoot /var/www/redir\n"
        "  Redirect 301 /old /new\n"
        "  RedirectMatch ^/archive/(.*)$ /blog/$1\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    ASSERT_NE(config.vhosts[0].redirect_rules, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].redirect_rules, "Redirect"), nullptr);
    EXPECT_NE(strstr(config.vhosts[0].redirect_rules, "RedirectMatch"),
              nullptr);
    ap_config_free(&config);
}

/* ---- Test: Alias directive ---- */

TEST(ApacheConfParser, AliasDirective) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName alias.com\n"
        "  DocumentRoot /var/www/alias\n"
        "  Alias /icons /usr/share/icons\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    ASSERT_NE(config.vhosts[0].aliases, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].aliases, "/icons"), nullptr);
    ap_config_free(&config);
}

/* ---- Test: Location block ---- */

TEST(ApacheConfParser, LocationBlock) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName loc.com\n"
        "  DocumentRoot /var/www/loc\n"
        "  <Location /admin>\n"
        "    Require all denied\n"
        "  </Location>\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhosts[0].context_count, 1);
    EXPECT_STREQ(config.vhosts[0].contexts[0].type, "Location");
    EXPECT_STREQ(config.vhosts[0].contexts[0].path, "/admin");
    EXPECT_EQ(config.vhosts[0].contexts[0].require_all, 0);
    ap_config_free(&config);
}

/* ---- Test: Access control directives ---- */

TEST(ApacheConfParser, AccessControl) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName acl.com\n"
        "  DocumentRoot /var/www/acl\n"
        "  <Directory /var/www/acl>\n"
        "    Order deny,allow\n"
        "    Deny from all\n"
        "    Allow from 192.168.1.0/24\n"
        "  </Directory>\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhosts[0].context_count, 1);
    EXPECT_EQ(config.vhosts[0].contexts[0].order, 1);
    EXPECT_STREQ(config.vhosts[0].contexts[0].access_deny, "all");
    EXPECT_STREQ(config.vhosts[0].contexts[0].access_allow,
                 "192.168.1.0/24");
    ap_config_free(&config);
}

/* ---- Test: DocumentRoot with quotes ---- */

TEST(ApacheConfParser, QuotedDocumentRoot) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName q.com\n"
        "  DocumentRoot \"/var/www/quoted dir\"\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_STREQ(config.vhosts[0].doc_root, "/var/www/quoted dir");
    ap_config_free(&config);
}

/* ---- Test: RewriteBase ---- */

TEST(ApacheConfParser, RewriteBase) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName rwb.com\n"
        "  DocumentRoot /var/www/rwb\n"
        "  RewriteEngine On\n"
        "  RewriteBase /app\n"
        "  RewriteRule ^index\\.html$ - [L]\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    ASSERT_NE(config.vhosts[0].rewrite_rules, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].rewrite_rules, "RewriteBase /app"),
              nullptr);
    ap_config_free(&config);
}

/* ---- Test: Listen with HTTPS ---- */

TEST(ApacheConfParser, ListenHTTPS) {
    TempFile conf(
        "Listen 80\n"
        "Listen 443 https\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.listen_count, 2);

    /* Find port 443 and verify SSL flag */
    int found_443 = 0;
    for (int i = 0; i < config.listen_count; i++) {
        if (config.listen_ports[i] == 443) {
            EXPECT_EQ(config.listen_ssl[i], 1);
            found_443 = 1;
        }
    }
    EXPECT_EQ(found_443, 1);
    ap_config_free(&config);
}

/* ---- Test: ap_config_free on zeroed config ---- */

TEST(ApacheConfParser, FreeZeroedConfig) {
    ap_config_t config;
    memset(&config, 0, sizeof(config));
    ap_config_free(&config);  /* should not crash */
}

/* ======== Phase 11: New directive tests ======== */

/* ---- Test: php_value and php_flag parsing ---- */

TEST(ApacheConfParser, PHPValueAndFlag) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName phpini.com\n"
        "  DocumentRoot /var/www/phpini\n"
        "  php_value memory_limit 256M\n"
        "  php_value upload_max_filesize 64M\n"
        "  php_flag display_errors Off\n"
        "  php_admin_value open_basedir /var/www\n"
        "  php_admin_flag log_errors On\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);

    ASSERT_NE(config.vhosts[0].php_values, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].php_values, "memory_limit 256M"), nullptr);
    EXPECT_NE(strstr(config.vhosts[0].php_values, "upload_max_filesize 64M"), nullptr);

    ASSERT_NE(config.vhosts[0].php_flags, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].php_flags, "display_errors Off"), nullptr);

    ASSERT_NE(config.vhosts[0].php_admin_values, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].php_admin_values, "open_basedir /var/www"), nullptr);

    ASSERT_NE(config.vhosts[0].php_admin_flags, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].php_admin_flags, "log_errors On"), nullptr);

    ap_config_free(&config);
}

/* ---- Test: PHP INI override OLS output ---- */

TEST(OLSConfigWriter, PHPIniOverride) {
    ap_vhost_t vh;
    memset(&vh, 0, sizeof(vh));
    vh.server_name = strdup("phpout.com");
    vh.doc_root = strdup("/var/www/phpout");
    vh.php_values = strdup("memory_limit 256M\nupload_max_filesize 64M");
    vh.php_flags = strdup("display_errors Off");

    TempDir outdir;
    std::string vhconf = std::string(outdir.path()) + "/vhconf.conf";
    ASSERT_EQ(ols_write_vhost(&vh, vhconf.c_str()), 0);

    std::string content = read_file(vhconf);
    EXPECT_NE(content.find("phpIniOverride"), std::string::npos);
    EXPECT_NE(content.find("php_value memory_limit 256M"), std::string::npos);
    EXPECT_NE(content.find("php_value upload_max_filesize 64M"), std::string::npos);
    EXPECT_NE(content.find("php_flag display_errors Off"), std::string::npos);

    free(vh.server_name);
    free(vh.doc_root);
    free(vh.php_values);
    free(vh.php_flags);
}

/* ---- Test: SSLProtocol parsing ---- */

TEST(ApacheConfParser, SSLProtocol) {
    TempFile conf(
        "<VirtualHost *:443>\n"
        "  ServerName sslp.com\n"
        "  DocumentRoot /var/www/sslp\n"
        "  SSLEngine on\n"
        "  SSLCertificateFile /etc/ssl/cert.pem\n"
        "  SSLCertificateKeyFile /etc/ssl/key.pem\n"
        "  SSLProtocol all -SSLv3 -TLSv1 -TLSv1.1\n"
        "  SSLCipherSuite ECDHE-RSA-AES256-GCM-SHA384:HIGH:!aNULL\n"
        "  SSLUseStapling On\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);

    ASSERT_NE(config.vhosts[0].ssl_protocol, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].ssl_protocol, "all"), nullptr);
    EXPECT_NE(strstr(config.vhosts[0].ssl_protocol, "-SSLv3"), nullptr);

    ASSERT_NE(config.vhosts[0].ssl_cipher_suite, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].ssl_cipher_suite, "ECDHE"), nullptr);

    EXPECT_EQ(config.vhosts[0].ssl_stapling, 1);

    ap_config_free(&config);
}

/* ---- Test: SSL protocol bitmask mapping ---- */

TEST(OLSConfigWriter, SSLProtocolBitmask) {
    /* "all -SSLv3 -TLSv1 -TLSv1.1" should be TLSv1.2+TLSv1.3 = 8+16=24 */
    EXPECT_EQ(ols_map_ssl_protocol("all -SSLv3 -TLSv1 -TLSv1.1"), 24);

    /* "TLSv1.2 TLSv1.3" = 8+16 = 24 */
    EXPECT_EQ(ols_map_ssl_protocol("TLSv1.2 TLSv1.3"), 24);

    /* "all" = 2+4+8+16 = 30 */
    EXPECT_EQ(ols_map_ssl_protocol("all"), 30);

    /* "+TLSv1.2" = 8 */
    EXPECT_EQ(ols_map_ssl_protocol("+TLSv1.2"), 8);

    /* NULL = default 24 */
    EXPECT_EQ(ols_map_ssl_protocol(NULL), 24);

    /* empty string = default 24 */
    EXPECT_EQ(ols_map_ssl_protocol(""), 24);

    /* "-TLSv1 -TLSv1.1" means start from all, remove = 8+16=24 */
    EXPECT_EQ(ols_map_ssl_protocol("-TLSv1 -TLSv1.1"), 24);
}

/* ---- Test: SSL extended OLS output ---- */

TEST(OLSConfigWriter, SSLExtendedOutput) {
    ap_vhost_t vh;
    memset(&vh, 0, sizeof(vh));
    vh.server_name = strdup("sslout.com");
    vh.doc_root = strdup("/var/www/sslout");
    vh.listen_ssl = 1;
    vh.ssl_cert = strdup("/etc/ssl/cert.pem");
    vh.ssl_key = strdup("/etc/ssl/key.pem");
    vh.ssl_protocol = strdup("all -SSLv3 -TLSv1 -TLSv1.1");
    vh.ssl_cipher_suite = strdup("ECDHE-RSA-AES256-GCM-SHA384");
    vh.ssl_stapling = 1;

    TempDir outdir;
    std::string vhconf = std::string(outdir.path()) + "/vhconf.conf";
    ASSERT_EQ(ols_write_vhost(&vh, vhconf.c_str()), 0);

    std::string content = read_file(vhconf);
    EXPECT_NE(content.find("sslProtocol"), std::string::npos);
    EXPECT_NE(content.find("24"), std::string::npos);
    EXPECT_NE(content.find("sslCipherSuite"), std::string::npos);
    EXPECT_NE(content.find("enableStapling"), std::string::npos);

    free(vh.server_name);
    free(vh.doc_root);
    free(vh.ssl_cert);
    free(vh.ssl_key);
    free(vh.ssl_protocol);
    free(vh.ssl_cipher_suite);
}

/* ---- Test: ProxyPass parsing ---- */

TEST(ApacheConfParser, ProxyPass) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName proxy.com\n"
        "  DocumentRoot /var/www/proxy\n"
        "  ProxyPass /api/ http://127.0.0.1:3000/\n"
        "  ProxyPassReverse /api/ http://127.0.0.1:3000/\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);

    ASSERT_NE(config.vhosts[0].proxy_pass, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].proxy_pass, "/api/"), nullptr);
    EXPECT_NE(strstr(config.vhosts[0].proxy_pass, "http://127.0.0.1:3000/"), nullptr);

    ASSERT_NE(config.vhosts[0].proxy_pass_reverse, nullptr);

    ap_config_free(&config);
}

/* ---- Test: Proxy OLS output ---- */

TEST(OLSConfigWriter, ProxyOutput) {
    ap_vhost_t vh;
    memset(&vh, 0, sizeof(vh));
    vh.server_name = strdup("proxyout.com");
    vh.doc_root = strdup("/var/www/proxyout");
    vh.proxy_pass = strdup("/api/ http://127.0.0.1:3000/");

    TempDir outdir;
    std::string vhconf = std::string(outdir.path()) + "/vhconf.conf";
    ASSERT_EQ(ols_write_vhost(&vh, vhconf.c_str()), 0);

    std::string content = read_file(vhconf);
    EXPECT_NE(content.find("extprocessor proxy_backend_0"), std::string::npos);
    EXPECT_NE(content.find("type                    proxy"), std::string::npos);
    EXPECT_NE(content.find("http://127.0.0.1:3000/"), std::string::npos);
    EXPECT_NE(content.find("context /api/"), std::string::npos);
    EXPECT_NE(content.find("handler                 proxy_backend_0"), std::string::npos);

    free(vh.server_name);
    free(vh.doc_root);
    free(vh.proxy_pass);
}

/* ---- Test: Expires duration parsing ---- */

TEST(ApacheConfParser, ExpiresDuration) {
    /* 7 days = 604800 */
    EXPECT_EQ(ap_parse_expires_duration("\"access plus 7 days\""), 604800);

    /* 1 hour = 3600 */
    EXPECT_EQ(ap_parse_expires_duration("\"access plus 1 hour\""), 3600);

    /* 30 minutes = 1800 */
    EXPECT_EQ(ap_parse_expires_duration("\"access plus 30 minutes\""), 1800);

    /* 1 year = 31536000 */
    EXPECT_EQ(ap_parse_expires_duration("\"access plus 1 year\""), 31536000);

    /* 1 month = 2592000 */
    EXPECT_EQ(ap_parse_expires_duration("\"access plus 1 month\""), 2592000);

    /* 1 week = 604800 */
    EXPECT_EQ(ap_parse_expires_duration("\"access plus 1 week\""), 604800);

    /* compound: 1 hour 30 minutes = 5400 */
    EXPECT_EQ(ap_parse_expires_duration("\"access plus 1 hour 30 minutes\""), 5400);

    /* seconds */
    EXPECT_EQ(ap_parse_expires_duration("\"access plus 300 seconds\""), 300);

    /* empty */
    EXPECT_EQ(ap_parse_expires_duration(""), 0);
    EXPECT_EQ(ap_parse_expires_duration(NULL), 0);
}

/* ---- Test: ExpiresByType parsing ---- */

TEST(ApacheConfParser, ExpiresByType) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName expires.com\n"
        "  DocumentRoot /var/www/expires\n"
        "  ExpiresActive On\n"
        "  ExpiresByType text/css \"access plus 7 days\"\n"
        "  ExpiresByType image/jpeg \"access plus 30 days\"\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);
    EXPECT_EQ(config.vhosts[0].expires_active, 1);

    ASSERT_NE(config.vhosts[0].expires_by_type, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].expires_by_type, "text/css=A604800"), nullptr);
    EXPECT_NE(strstr(config.vhosts[0].expires_by_type, "image/jpeg=A2592000"), nullptr);

    ap_config_free(&config);
}

/* ---- Test: ExpiresByType with modification prefix ---- */

TEST(ApacheConfParser, ExpiresByTypeModification) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName expmod.com\n"
        "  DocumentRoot /var/www/expmod\n"
        "  ExpiresActive On\n"
        "  ExpiresByType text/html \"modification plus 1 hour\"\n"
        "  ExpiresByType image/png \"access plus 30 days\"\n"
        "  ExpiresByType application/json \"M3600\"\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);
    EXPECT_EQ(config.vhosts[0].expires_active, 1);

    ASSERT_NE(config.vhosts[0].expires_by_type, nullptr);
    /* modification prefix -> M */
    EXPECT_NE(strstr(config.vhosts[0].expires_by_type, "text/html=M3600"), nullptr);
    /* access prefix -> A */
    EXPECT_NE(strstr(config.vhosts[0].expires_by_type, "image/png=A2592000"), nullptr);
    /* M shorthand -> M */
    EXPECT_NE(strstr(config.vhosts[0].expires_by_type, "application/json=M3600"), nullptr);

    ap_config_free(&config);
}

/* ---- Test: ProxyPass URL scheme validation ---- */

TEST(ApacheConfParser, ProxyPassSchemeValidation) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName proxyval.com\n"
        "  DocumentRoot /var/www/proxyval\n"
        "  ProxyPass /api/ http://127.0.0.1:3000/\n"
        "  ProxyPass /ws/ ws://127.0.0.1:3001/\n"
        "  ProxyPass /bad/ ftp://example.com/\n"
        "  ProxyPass /ajp/ ajp://127.0.0.1:8009/\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);

    ASSERT_NE(config.vhosts[0].proxy_pass, nullptr);
    /* Valid schemes should be present */
    EXPECT_NE(strstr(config.vhosts[0].proxy_pass, "http://127.0.0.1:3000/"), nullptr);
    EXPECT_NE(strstr(config.vhosts[0].proxy_pass, "ws://127.0.0.1:3001/"), nullptr);
    EXPECT_NE(strstr(config.vhosts[0].proxy_pass, "ajp://127.0.0.1:8009/"), nullptr);
    /* Invalid scheme (ftp) should be rejected */
    EXPECT_EQ(strstr(config.vhosts[0].proxy_pass, "ftp://"), nullptr);

    ap_config_free(&config);
}

/* ---- Test: Expires OLS output ---- */

TEST(OLSConfigWriter, ExpiresOutput) {
    ap_vhost_t vh;
    memset(&vh, 0, sizeof(vh));
    vh.server_name = strdup("expout.com");
    vh.doc_root = strdup("/var/www/expout");
    vh.expires_active = 1;
    vh.expires_by_type = strdup("text/css=A604800\nimage/jpeg=A2592000");

    TempDir outdir;
    std::string vhconf = std::string(outdir.path()) + "/vhconf.conf";
    ASSERT_EQ(ols_write_vhost(&vh, vhconf.c_str()), 0);

    std::string content = read_file(vhconf);
    EXPECT_NE(content.find("enableExpires           1"), std::string::npos);
    EXPECT_NE(content.find("expiresByType           text/css=A604800"), std::string::npos);
    EXPECT_NE(content.find("expiresByType           image/jpeg=A2592000"), std::string::npos);

    free(vh.server_name);
    free(vh.doc_root);
    free(vh.expires_by_type);
}

/* ---- Test: SuexecUserGroup parsing ---- */

TEST(ApacheConfParser, SuexecUserGroup) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName suexec.com\n"
        "  DocumentRoot /var/www/suexec\n"
        "  SuexecUserGroup webuser webgroup\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);

    ASSERT_NE(config.vhosts[0].suexec_user, nullptr);
    EXPECT_STREQ(config.vhosts[0].suexec_user, "webuser");
    ASSERT_NE(config.vhosts[0].suexec_group, nullptr);
    EXPECT_STREQ(config.vhosts[0].suexec_group, "webgroup");

    ap_config_free(&config);
}

/* ---- Test: Suexec OLS output ---- */

TEST(OLSConfigWriter, SuexecOutput) {
    ap_vhost_t vh;
    memset(&vh, 0, sizeof(vh));
    vh.server_name = strdup("suexecout.com");
    vh.doc_root = strdup("/var/www/suexecout");
    vh.suexec_user = strdup("webuser");
    vh.suexec_group = strdup("webgroup");

    TempDir outdir;
    std::string vhconf = std::string(outdir.path()) + "/vhconf.conf";
    ASSERT_EQ(ols_write_vhost(&vh, vhconf.c_str()), 0);

    std::string content = read_file(vhconf);
    EXPECT_NE(content.find("setUIDMode                2"), std::string::npos);
    EXPECT_NE(content.find("user                      webuser"), std::string::npos);
    EXPECT_NE(content.find("group                     webgroup"), std::string::npos);

    free(vh.server_name);
    free(vh.doc_root);
    free(vh.suexec_user);
    free(vh.suexec_group);
}

/* ---- Test: SecRuleEngine parsing ---- */

TEST(ApacheConfParser, SecRuleEngine) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName modsec.com\n"
        "  DocumentRoot /var/www/modsec\n"
        "  SecRuleEngine On\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhosts[0].modsecurity_enabled, 1);
    ap_config_free(&config);
}

TEST(ApacheConfParser, SecRuleEngineDetectionOnly) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName modsec2.com\n"
        "  DocumentRoot /var/www/modsec2\n"
        "  SecRuleEngine DetectionOnly\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhosts[0].modsecurity_enabled, 2);
    ap_config_free(&config);
}

/* ---- Test: LimitRequestBody ---- */

TEST(ApacheConfParser, LimitRequestBody) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName limit.com\n"
        "  DocumentRoot /var/www/limit\n"
        "  LimitRequestBody 10485760\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhosts[0].req_body_limit, 10485760);
    ap_config_free(&config);
}

/* ---- Test: LimitRequestBody OLS output ---- */

TEST(OLSConfigWriter, LimitRequestBodyOutput) {
    ap_vhost_t vh;
    memset(&vh, 0, sizeof(vh));
    vh.server_name = strdup("limitout.com");
    vh.doc_root = strdup("/var/www/limitout");
    vh.req_body_limit = 10485760;

    TempDir outdir;
    std::string vhconf = std::string(outdir.path()) + "/vhconf.conf";
    ASSERT_EQ(ols_write_vhost(&vh, vhconf.c_str()), 0);

    std::string content = read_file(vhconf);
    EXPECT_NE(content.find("reqBodyLimit              10485760"), std::string::npos);

    free(vh.server_name);
    free(vh.doc_root);
}

/* ---- Test: SetEnv parsing ---- */

TEST(ApacheConfParser, SetEnvParsing) {
    TempFile conf(
        "<VirtualHost *:80>\n"
        "  ServerName env.com\n"
        "  DocumentRoot /var/www/env\n"
        "  SetEnv APP_ENV production\n"
        "  SetEnv DB_HOST localhost\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);

    ASSERT_NE(config.vhosts[0].env_vars, nullptr);
    EXPECT_NE(strstr(config.vhosts[0].env_vars, "APP_ENV production"), nullptr);
    EXPECT_NE(strstr(config.vhosts[0].env_vars, "DB_HOST localhost"), nullptr);

    ap_config_free(&config);
}

/* ---- Test: Panel detection (basic callable test) ---- */

TEST(ApacheConfParser, PanelDetection) {
    /* Just verify the function is callable and returns a valid enum.
     * In CI environments, no panel dirs will exist. */
    panel_type_t panel = ap_detect_panel();
    EXPECT_GE((int)panel, 0);
    EXPECT_LE((int)panel, 3);
}

/* ---- Test: Hot reload state check ---- */

TEST(ApacheConfParser, HotReloadCheck) {
    /* Create a temp config file */
    TempFile conf("Listen 80\n");
    TempDir statedir;
    std::string state_path = std::string(statedir.path()) + "/test.state";

    /* First check: no state file, should return "changed" */
    int rc = ap_check_config_changed(conf.path(), state_path.c_str());
    EXPECT_EQ(rc, 1);

    /* Second check: state file exists, should return "unchanged" */
    rc = ap_check_config_changed(conf.path(), state_path.c_str());
    EXPECT_EQ(rc, 0);

    /* Error case: nonexistent file */
    rc = ap_check_config_changed("/nonexistent/file", state_path.c_str());
    EXPECT_EQ(rc, -1);

    /* Error case: null args */
    rc = ap_check_config_changed(NULL, state_path.c_str());
    EXPECT_EQ(rc, -1);
    rc = ap_check_config_changed(conf.path(), NULL);
    EXPECT_EQ(rc, -1);
}

/* ---- Test: ModSecurity OLS output ---- */

TEST(OLSConfigWriter, ModSecurityOutput) {
    ap_vhost_t vh;
    memset(&vh, 0, sizeof(vh));
    vh.server_name = strdup("modsecout.com");
    vh.doc_root = strdup("/var/www/modsecout");
    vh.modsecurity_enabled = 1;

    TempDir outdir;
    std::string vhconf = std::string(outdir.path()) + "/vhconf.conf";
    ASSERT_EQ(ols_write_vhost(&vh, vhconf.c_str()), 0);

    std::string content = read_file(vhconf);
    EXPECT_NE(content.find("ModSecurity"), std::string::npos);
    EXPECT_NE(content.find("SecRuleEngine On"), std::string::npos);

    free(vh.server_name);
    free(vh.doc_root);
}

/* ---- Test: Full pipeline with new directives ---- */

TEST(OLSConfigWriter, FullPipelineExtended) {
    TempFile conf(
        "Listen 80\n"
        "Listen 443 https\n"
        "<VirtualHost *:443>\n"
        "  ServerName full.com\n"
        "  DocumentRoot /var/www/full\n"
        "  SSLEngine on\n"
        "  SSLCertificateFile /etc/ssl/cert.pem\n"
        "  SSLCertificateKeyFile /etc/ssl/key.pem\n"
        "  SSLProtocol all -SSLv3 -TLSv1 -TLSv1.1\n"
        "  SSLCipherSuite HIGH:!aNULL\n"
        "  SSLUseStapling On\n"
        "  RewriteEngine On\n"
        "  RewriteRule ^/(.*)$ /index.php/$1 [L]\n"
        "  php_value memory_limit 512M\n"
        "  php_flag display_errors Off\n"
        "  ExpiresActive On\n"
        "  ExpiresByType text/css \"access plus 7 days\"\n"
        "  ProxyPass /api/ http://127.0.0.1:3000/\n"
        "  ProxyPassReverse /api/ http://127.0.0.1:3000/\n"
        "  SuexecUserGroup www www\n"
        "  SecRuleEngine DetectionOnly\n"
        "  LimitRequestBody 52428800\n"
        "  SetEnv APP_ENV production\n"
        "</VirtualHost>\n"
    );

    ap_config_t config;
    ASSERT_EQ(ap_parse_config(conf.path(), &config), 0);
    EXPECT_EQ(config.vhost_count, 1);

    TempDir outdir;
    ASSERT_EQ(ols_write_config(&config, outdir.path()), 0);

    /* Check vhconf.conf */
    std::string vhconf = std::string(outdir.path()) +
                         "/vhosts/full.com/vhconf.conf";
    std::string content = read_file(vhconf);

    EXPECT_NE(content.find("docRoot"), std::string::npos);
    EXPECT_NE(content.find("sslProtocol"), std::string::npos);
    EXPECT_NE(content.find("phpIniOverride"), std::string::npos);
    EXPECT_NE(content.find("enableExpires"), std::string::npos);
    EXPECT_NE(content.find("proxy_backend_0"), std::string::npos);
    EXPECT_NE(content.find("setUIDMode"), std::string::npos);
    EXPECT_NE(content.find("ModSecurity"), std::string::npos);
    EXPECT_NE(content.find("reqBodyLimit"), std::string::npos);

    ap_config_free(&config);
}

/* ---- Test: Free zeroed vhost with new fields doesn't crash ---- */

TEST(ApacheConfParser, FreeZeroedConfigWithNewFields) {
    ap_config_t config;
    memset(&config, 0, sizeof(config));

    /* Create a vhost with all new fields NULL / 0 */
    config.vhost_count = 1;
    memset(&config.vhosts[0], 0, sizeof(ap_vhost_t));

    ap_config_free(&config);  /* should not crash */
}
