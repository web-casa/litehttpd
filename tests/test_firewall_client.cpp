/**
 * test_firewall_client.cpp — unit tests for the firewall UDS client.
 *
 * Spins up an in-process stub daemon (a thread accepting one connection,
 * reading a line, replying "ok\n") to verify the exact wire protocol the
 * Rust litehttpd-firewalld daemon speaks:
 *   block   → "b,<ip>[,ttl]\n"
 *   unblock → "u,<ip>\n"
 * and that every failure mode (no daemon, bad reply, garbage IP rejection)
 * degrades to -1 so the caller keeps its application-level behaviour.
 */
#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

extern "C" {
#include "htaccess_firewall_client.h"
}

namespace {

class StubDaemon {
public:
    explicit StubDaemon(std::string reply) : reply_(std::move(reply)) {}

    ~StubDaemon() { stop(); }

    bool start(const std::string &path)
    {
        path_ = path;
        ::unlink(path_.c_str());
        listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_fd_ < 0)
            return false;
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path_.c_str());
        if (::bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr),
                   sizeof(addr)) < 0 ||
            ::listen(listen_fd_, 4) < 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
        running_ = true;
        accept_thread_ = std::thread([this] { accept_loop(); });
        return true;
    }

    void stop()
    {
        if (!running_.exchange(false))
            return;
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        if (accept_thread_.joinable())
            accept_thread_.join();
        if (!path_.empty())
            ::unlink(path_.c_str());
    }

    /* Last line received from a client, guarded for cross-thread reads. */
    std::string last_command() const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return last_cmd_;
    }

private:
    void accept_loop()
    {
        while (running_) {
            int c = ::accept(listen_fd_, nullptr, nullptr);
            if (c < 0)
                break;
            char buf[256];
            ssize_t n = ::read(c, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                std::lock_guard<std::mutex> lk(mtx_);
                last_cmd_ = buf;
            }
            /* Drain until newline so partial-line timing can't flake. */
            if (n > 0 && !strchr(buf, '\n')) {
                ssize_t m = ::read(c, buf, sizeof(buf) - 1);
                (void)m;
            }
            (void)::write(c, reply_.c_str(), reply_.size());
            ::close(c);
        }
    }

    std::string reply_;
    std::string path_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    mutable std::mutex mtx_;
    std::string last_cmd_;
};

class FirewallClientTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        char templ[] = "/tmp/litehttpd-fwtest-XXXXXX";
        char *dir = mkdtemp(templ);
        ASSERT_NE(dir, nullptr);
        sock_path_ = std::string(dir) + "/firewall.sock";
        /* Route the client at our temp socket (daemon honours the same var) */
        old_sock_ = getenv("LITEHTTPD_FW_SOCK");
        setenv("LITEHTTPD_FW_SOCK", sock_path_.c_str(), 1);
    }

    void TearDown() override
    {
        if (old_sock_)
            setenv("LITEHTTPD_FW_SOCK", old_sock_, 1);
        else
            unsetenv("LITEHTTPD_FW_SOCK");
        if (!sock_path_.empty())
            ::unlink(sock_path_.c_str());
    }

    std::string sock_path_;
    const char *old_sock_ = nullptr;
};

} // namespace

TEST_F(FirewallClientTest, NoDaemon_BlockFailsGracefully)
{
    /* Socket file absent — must be a clean -1, not a crash or hang. */
    EXPECT_EQ(fw_client_block("203.0.113.9", 300), -1);
    EXPECT_EQ(fw_client_unblock("203.0.113.9"), -1);
    EXPECT_EQ(fw_client_init(), -1);
    EXPECT_EQ(fw_client_is_available(), 0);
}

TEST_F(FirewallClientTest, BlockSendsExactWireFormat)
{
    StubDaemon daemon("ok\n");
    ASSERT_TRUE(daemon.start(sock_path_));

    EXPECT_EQ(fw_client_block("203.0.113.9", 300), 0);
    EXPECT_EQ(daemon.last_command(), "b,203.0.113.9,300\n");
}

TEST_F(FirewallClientTest, BlockWithoutTtlOmitsTtlField)
{
    StubDaemon daemon("ok\n");
    ASSERT_TRUE(daemon.start(sock_path_));

    EXPECT_EQ(fw_client_block("2001:db8::1", 0), 0);
    EXPECT_EQ(daemon.last_command(), "b,2001:db8::1\n");
}

TEST_F(FirewallClientTest, UnblockSendsExactWireFormat)
{
    StubDaemon daemon("ok\n");
    ASSERT_TRUE(daemon.start(sock_path_));

    EXPECT_EQ(fw_client_unblock("203.0.113.9"), 0);
    EXPECT_EQ(daemon.last_command(), "u,203.0.113.9\n");
}

TEST_F(FirewallClientTest, DaemonErrorReplyIsFailure)
{
    StubDaemon daemon("err,invalid-ip\n");
    ASSERT_TRUE(daemon.start(sock_path_));

    EXPECT_EQ(fw_client_block("203.0.113.9", 60), -1);
}

TEST_F(FirewallClientTest, GarbageInputRejectedLocally)
{
    StubDaemon daemon("ok\n");
    ASSERT_TRUE(daemon.start(sock_path_));

    EXPECT_EQ(fw_client_block(nullptr, 60), -1);
    EXPECT_EQ(fw_client_block("", 60), -1);
    EXPECT_EQ(fw_client_unblock(nullptr), -1);
}

TEST_F(FirewallClientTest, LongIPTruncatedFormatIsRejected)
{
    /* An over-long "IP" would be truncated by snprintf — the client must
     * refuse to send a truncated command rather than block the wrong target. */
    std::string junk(200, '9');
    EXPECT_EQ(fw_client_block(junk.c_str(), 60), -1);
}
