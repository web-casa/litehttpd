#include <gtest/gtest.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Extract ratelimit_parse directly (no LSI/SHM deps) */
typedef struct {
    int requests; int window_secs; int burst; int action;
} ratelimit_config_t;

static ratelimit_config_t ratelimit_parse(const char *value)
{
    ratelimit_config_t cfg = { .requests = 0, .window_secs = 60, .burst = 0, .action = 0 };
    if (!value) return cfg;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", value);
    char *slash = strchr(buf, '/');
    if (!slash) return cfg;
    *slash = '\0';
    cfg.requests = atoi(buf);
    cfg.window_secs = atoi(slash + 1);
    if (cfg.requests <= 0) cfg.requests = 100;
    if (cfg.window_secs <= 0) cfg.window_secs = 60;
    char *burst = strstr(slash + 1, "burst=");
    if (burst) cfg.burst = atoi(burst + 6);
    if (cfg.burst <= 0) cfg.burst = cfg.requests;
    if (strstr(slash + 1, "action=throttle")) cfg.action = 1;
    else if (strstr(slash + 1, "action=log")) cfg.action = 2;
    return cfg;
}

TEST(RateLimitParse, Basic) {
    auto cfg = ratelimit_parse("100/60");
    EXPECT_EQ(cfg.requests, 100);
    EXPECT_EQ(cfg.window_secs, 60);
    EXPECT_EQ(cfg.action, 0);
}
TEST(RateLimitParse, WithBurst) {
    auto cfg = ratelimit_parse("1000/3600 burst=200 action=throttle");
    EXPECT_EQ(cfg.requests, 1000);
    EXPECT_EQ(cfg.window_secs, 3600);
    EXPECT_EQ(cfg.burst, 200);
    EXPECT_EQ(cfg.action, 1);
}
TEST(RateLimitParse, LogOnly) {
    auto cfg = ratelimit_parse("50/30 action=log");
    EXPECT_EQ(cfg.action, 2);
}
TEST(RateLimitParse, Invalid) {
    auto cfg = ratelimit_parse("10");
    EXPECT_EQ(cfg.requests, 0);
}
TEST(RateLimitParse, NegativeClamped) {
    auto cfg = ratelimit_parse("-1/-1");
    EXPECT_EQ(cfg.requests, 100);
    EXPECT_EQ(cfg.window_secs, 60);
}
