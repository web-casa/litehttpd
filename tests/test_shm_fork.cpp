#include <gtest/gtest.h>
extern "C" {
#include "htaccess_shm.h"
}
#include <sys/wait.h>
#include <unistd.h>

TEST(ShmForkTest, ConcurrentIncrementSameIP) {
    ASSERT_EQ(shm_init("/tmp/litehttpd-test-shm", 1024), 0);
    const char *ip = "10.99.99.99";
    time_t now = time(NULL);

    brute_force_record_t rec = {};
    rec.attempt_count = 0;
    ASSERT_EQ(shm_update_record(ip, &rec), 0);

    pid_t pids[4];
    for (int w = 0; w < 4; w++) {
        pids[w] = fork();
        if (pids[w] == 0) {
            brute_force_record_t out;
            for (int i = 0; i < 200; i++)
                shm_atomic_increment(ip, now, &out);
            _exit(0);
        }
    }
    for (int w = 0; w < 4; w++) {
        int st; waitpid(pids[w], &st, 0);
        EXPECT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }
    brute_force_record_t *final = shm_get_record(ip);
    ASSERT_TRUE(final != NULL);
    EXPECT_GE(final->attempt_count, 0);
}

TEST(ShmForkTest, BlockVisibleCrossProcess) {
    ASSERT_EQ(shm_init("/tmp/litehttpd-test-shm", 1024), 0);
    const char *ip = "192.168.100.100";
    time_t now = time(NULL);
    brute_force_record_t rec = {};
    rec.blocked_until = now + 3600;
    rec.attempt_count = 5;
    ASSERT_EQ(shm_update_record(ip, &rec), 0);

    pid_t pid = fork();
    if (pid == 0) {
        brute_force_record_t *r = shm_get_record(ip);
        _exit(r && r->blocked_until > now ? 0 : 1);
    }
    int st; waitpid(pid, &st, 0);
    EXPECT_EQ(WEXITSTATUS(st), 0);
}
