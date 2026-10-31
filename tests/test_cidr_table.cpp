#include <gtest/gtest.h>
extern "C" {
#include "htaccess_cidr_table.h"
}

class CidrTableTest : public ::testing::Test {
protected:
    cidr_table_t t;
    void SetUp() override { memset(&t, 0, sizeof(t)); }
};

TEST_F(CidrTableTest, EmptyTableNoMatch) {
    EXPECT_EQ(cidr_table_match_v4(&t, 0x01020304), 0);
}

TEST_F(CidrTableTest, SingleV4Exact) {
    cidr_table_add_str(&t, "192.168.1.1");
    cidr_table_finalize(&t);
    /* 192.168.1.1 in host order for cidr_parse: stored as network address */
    uint32_t ip = (192 << 24) | (168 << 16) | (1 << 8) | 1;
    EXPECT_EQ(cidr_table_match_v4(&t, ip), 1);
    uint32_t other = (192 << 24) | (168 << 16) | (1 << 8) | 2;
    EXPECT_EQ(cidr_table_match_v4(&t, other), 0);
}

TEST_F(CidrTableTest, CidrRange24) {
    cidr_table_add_str(&t, "10.0.1.0/24");
    cidr_table_finalize(&t);
    uint32_t in1 = (10 << 24) | (0 << 16) | (1 << 8) | 1;
    uint32_t in2 = (10 << 24) | (0 << 16) | (1 << 8) | 254;
    uint32_t out = (10 << 24) | (0 << 16) | (2 << 8) | 1;
    EXPECT_EQ(cidr_table_match_v4(&t, in1), 1);
    EXPECT_EQ(cidr_table_match_v4(&t, in2), 1);
    EXPECT_EQ(cidr_table_match_v4(&t, out), 0);
}

TEST_F(CidrTableTest, MultipleEntries) {
    cidr_table_add_str(&t, "10.0.0.0/8");
    cidr_table_add_str(&t, "192.168.0.0/16");
    cidr_table_add_str(&t, "172.16.5.0/24");
    cidr_table_finalize(&t);
    EXPECT_EQ(cidr_table_match_v4(&t, (10<<24)|(1<<16)|(1<<8)|1), 1);
    EXPECT_EQ(cidr_table_match_v4(&t, (192<<24)|(168<<16)|(1<<8)|1), 1);
    EXPECT_EQ(cidr_table_match_v4(&t, (172<<24)|(16<<16)|(5<<8)|99), 1);
    EXPECT_EQ(cidr_table_match_v4(&t, (172<<24)|(16<<16)|(6<<8)|1), 0);
}

TEST_F(CidrTableTest, IPv6Basic) {
    uint8_t addr[16] = {0xfd, 0x00, 1,2,3,4,5,6,7,8,9,10,11,12,13,14};
    cidr_table_add_v6(&t, addr, 64);
    cidr_table_finalize(&t);
    uint8_t same_prefix[16];
    memcpy(same_prefix, addr, 16);
    same_prefix[15] = 0xff; /* different host, same /64 */
    EXPECT_EQ(cidr_table_match_v6(&t, same_prefix), 1);
    uint8_t diff_prefix[16] = {0xfe, 0x80, 0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    EXPECT_EQ(cidr_table_match_v6(&t, diff_prefix), 0);
}

TEST_F(CidrTableTest, ManyEntriesPerf) {
    for (int i = 0; i < 1000; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "10.%d.%d.0/24", i / 256, i % 256);
        cidr_table_add_str(&t, buf);
    }
    cidr_table_finalize(&t);
    EXPECT_EQ(t.v4_count, 1000);
    /* Match a known entry */
    uint32_t hit = (10<<24)|((500/256)<<16)|((500%256)<<8)|42;
    EXPECT_EQ(cidr_table_match_v4(&t, hit), 1);
    /* Non-match */
    uint32_t miss = (11<<24)|(1<<16)|(1<<8)|1;
    EXPECT_EQ(cidr_table_match_v4(&t, miss), 0);
}
