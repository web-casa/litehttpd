#include <gtest/gtest.h>
#include <string.h>
#include <stdlib.h>

extern "C" {
#include "htaccess_exec_cache.h"
}

TEST(CacheBridge, ParsePublic) {
    EXPECT_EQ(cache_bridge_parse_enable("public"), 1);
    EXPECT_EQ(cache_bridge_parse_enable("PUBLIC"), 1);
    EXPECT_EQ(cache_bridge_parse_enable("Public"), 1);
}

TEST(CacheBridge, ParsePrivate) {
    EXPECT_EQ(cache_bridge_parse_enable("private"), 2);
    EXPECT_EQ(cache_bridge_parse_enable("PRIVATE"), 2);
}

TEST(CacheBridge, ParseOff) {
    EXPECT_EQ(cache_bridge_parse_enable("off"), 0);
    EXPECT_EQ(cache_bridge_parse_enable(NULL), 0);
    EXPECT_EQ(cache_bridge_parse_enable("invalid"), 0);
}

TEST(CacheBridge, BuildHeaderPublic) {
    cache_bridge_config_t cfg = {};
    cfg.enabled = 1;
    cfg.default_expire = 3600;
    char buf[128];
    cache_bridge_build_header(&cfg, buf, sizeof(buf));
    EXPECT_STREQ(buf, "public,max-age=3600");
}

TEST(CacheBridge, BuildHeaderDefaultAge) {
    /* No expire specified → default 3600 */
    cache_bridge_config_t cfg = {};
    cfg.enabled = 1;
    char buf[128];
    cache_bridge_build_header(&cfg, buf, sizeof(buf));
    EXPECT_STREQ(buf, "public,max-age=3600");
}

TEST(CacheBridge, BuildHeaderPrivate) {
    cache_bridge_config_t cfg = {};
    cfg.enabled = 2;
    cfg.default_expire = 120;
    char buf[128];
    cache_bridge_build_header(&cfg, buf, sizeof(buf));
    EXPECT_STREQ(buf, "private,max-age=120");
}

TEST(CacheBridge, BuildHeaderOff) {
    cache_bridge_config_t cfg = {};
    cfg.enabled = 0;
    char buf[128];
    cache_bridge_build_header(&cfg, buf, sizeof(buf));
    EXPECT_STREQ(buf, "off");
}

TEST(CacheBridge, BuildHeaderMaxExpireOverrides) {
    cache_bridge_config_t cfg = {};
    cfg.enabled = 1;
    cfg.max_expire = 7200;
    cfg.default_expire = 3600;
    char buf[128];
    cache_bridge_build_header(&cfg, buf, sizeof(buf));
    EXPECT_STREQ(buf, "public,max-age=7200");
}


/* ==================================================================
 *  esi=on modifier (LSWS syntax)
 * ================================================================== */

TEST(CacheBridge, ParseEnableWithEsiModifier)
{
    EXPECT_EQ(cache_bridge_parse_enable("public,esi=on"), 1);
    EXPECT_EQ(cache_bridge_parse_enable("private,esi=on"), 2);
    EXPECT_EQ(cache_bridge_parse_enable("public"), 1);  /* bare still works */
}

TEST(CacheBridge, BuildHeaderWithEsiOn)
{
    cache_bridge_config_t cfg = {};
    cfg.enabled = 1;
    cfg.max_expire = 1800;
    cfg.esi = 1;
    char buf[128];
    cache_bridge_build_header(&cfg, buf, sizeof(buf));
    EXPECT_STREQ(buf, "public,max-age=1800,esi=on");
}

TEST(CacheBridge, BuildHeaderWithoutEsi)
{
    cache_bridge_config_t cfg = {};
    cfg.enabled = 1;
    cfg.max_expire = 1800;
    cfg.esi = 0;
    char buf[128];
    cache_bridge_build_header(&cfg, buf, sizeof(buf));
    EXPECT_STREQ(buf, "public,max-age=1800");
}
