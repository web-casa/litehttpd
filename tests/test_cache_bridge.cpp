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
