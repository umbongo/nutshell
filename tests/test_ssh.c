#include "test_framework.h"
#include "ssh_session.h"
#include "ssh_channel.h"
#include "ssh_pty.h"
#include "ssh_io.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#endif

int test_ssh_safety_checks(void) {
    TEST_BEGIN();

    // Test 1: Open channel on NULL session
    SSHChannel *ch = ssh_channel_open(NULL);
    ASSERT_TRUE(ch == NULL);

    // Test 2: Open channel on disconnected session
    SshSession *s = ssh_session_new();
    ASSERT_TRUE(s != NULL);
    ASSERT_FALSE(s->connected);
    
    ch = ssh_channel_open(s);
    ASSERT_TRUE(ch == NULL);

    ssh_session_free(s);

    TEST_END();
}

int test_ssh_channel_safety(void) {
    TEST_BEGIN();

    // Test free and write on NULL channel
    ssh_channel_free(NULL); // Should not crash
    ASSERT_EQ(ssh_channel_write(NULL, "test", 4), -1);
    
    TEST_END();
}

int test_ssh_io_safety(void) {
    TEST_BEGIN();

    // Test NULL inputs for IO poll
    ASSERT_EQ(ssh_io_poll(NULL, NULL, NULL), -1);

    // Test blocking mode setter safety
    SshSession *s = ssh_session_new();
    if (s) {
        // Should not crash even if session is not connected
        ssh_session_set_blocking(s, false);
        ssh_session_set_blocking(s, true);
        
        // If we could mock libssh2_session_init failure, we'd test s->session == NULL case,
        // but ssh_session_new aborts or returns NULL on failure.
        // We can manually test NULL session ptr
        ssh_session_set_blocking(NULL, false);
    }
    ssh_session_free(s);

    TEST_END();
}

int test_ssh_pty_safety(void) {
    TEST_BEGIN();

    // Test NULL channel inputs
    ASSERT_EQ(ssh_pty_request(NULL, "xterm", 80, 24), -1);
    ASSERT_EQ(ssh_pty_resize(NULL, 100, 30), -1);

    TEST_END();
}

int test_pty_resize_dedup(void) {
    TEST_BEGIN();

    /* Set up a channel with a fake (non-NULL) libssh2 channel pointer and
     * pre-loaded last_cols/last_rows.  Because the dedup guard fires before
     * any libssh2 call, the fake pointer never gets dereferenced. */
    SSHChannel ch;
    ch.channel   = (LIBSSH2_CHANNEL *)(void *)1; /* sentinel – must not be NULL */
    ch.ssh       = NULL;
    ch.last_cols = 80;
    ch.last_rows = 24;

    /* Same dimensions → early return 0, no libssh2 call, fields unchanged */
    ASSERT_EQ(ssh_pty_resize(&ch, 80, 24), 0);
    ASSERT_EQ(ch.last_cols, 80);
    ASSERT_EQ(ch.last_rows, 24);

    TEST_END();
}

int test_pty_resize_initial_state(void) {
    TEST_BEGIN();

    /* A freshly zeroed channel has last_cols == 0, last_rows == 0.
     * The NULL-channel guard runs before the dedup check, so any resize on a
     * channel with no libssh2 handle returns -1 — even a same-size one. */
    SSHChannel ch;
    ch.channel   = NULL;
    ch.ssh       = NULL;
    ch.last_cols = 0;
    ch.last_rows = 0;

    ASSERT_EQ(ssh_pty_resize(&ch, 0, 0), -1);  /* NULL channel wins over dedup */
    ASSERT_EQ(ssh_pty_resize(&ch, 80, 24), -1); /* NULL channel → -1 */

    TEST_END();
}

int test_ssh_connect_fail(void) {
    TEST_BEGIN();
    SshSession *s = ssh_session_new();
    
    // Attempt connection to invalid port/host (localhost:1)
    // This should fail gracefully without crashing
    int rc = ssh_connect(s, "127.0.0.1", 1); 
    ASSERT_EQ(rc, -1);
    ASSERT_FALSE(s->connected);
    // Check error message is set (not empty)
    ASSERT_TRUE(s->last_error[0] != '\0');
    
    ssh_session_free(s);
    TEST_END();
}

int test_ssh_session_blocking(void) {
    TEST_BEGIN();
    SshSession *s = ssh_session_new();
    
    // Should not crash on NULL session or disconnected session
    ssh_session_set_blocking(NULL, true);
    ssh_session_set_blocking(s, false);
    
    ssh_session_free(s);
    TEST_END();
}
int test_ssh_timeout_helpers_null_safe(void) {
    TEST_BEGIN();
    ssh_session_set_timeout(NULL, 0);          /* must not crash */
    ASSERT_EQ(ssh_session_timed_out(NULL), 0);

    SshSession *s = ssh_session_new();
    ASSERT_TRUE(s != NULL);
    ASSERT_EQ(ssh_session_timed_out(s), 0);    /* nothing has timed out yet */
    ssh_session_set_timeout(s, 0);
    ssh_session_set_timeout(s, SSH_SETUP_TIMEOUT_MS);
    ASSERT_EQ(ssh_session_timed_out(s), 0);
    ssh_session_free(s);
    TEST_END();
}

int test_ssh_handshake_timeout_silent_server(void) {
    TEST_BEGIN();
#ifndef _WIN32
    /* A listener that never accept()s: the kernel completes the TCP
     * handshake, the server never sends its SSH banner. */
    int lsock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(lsock >= 0);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t alen = (socklen_t)sizeof(addr);
    int ok = bind(lsock, (struct sockaddr *)&addr, sizeof(addr)) == 0
          && listen(lsock, 1) == 0
          && getsockname(lsock, (struct sockaddr *)&addr, &alen) == 0;
    if (!ok) {
        close(lsock);
        ASSERT_TRUE(ok);
    }
    int port = (int)ntohs(addr.sin_port);

    SshSession *s = ssh_session_new();
    if (!s) {
        close(lsock);
        ASSERT_TRUE(s != NULL);
    }

    ssh_setup_timeout_override(300);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int rc = ssh_connect(s, "127.0.0.1", port);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ssh_setup_timeout_override(0);
    close(lsock);   /* before any assertion can return early */

    long elapsed_ms = (long)(t1.tv_sec - t0.tv_sec) * 1000L
                    + (t1.tv_nsec - t0.tv_nsec) / 1000000L;
    int timed_out = ssh_session_timed_out(s);
    int has_text = strstr(s->last_error, "during the SSH handshake") != NULL;
    int connected = s->connected;
    ssh_session_free(s);

    ASSERT_EQ(rc, -1);
    ASSERT_FALSE(connected);
    ASSERT_TRUE(elapsed_ms >= 250);
    ASSERT_TRUE(elapsed_ms < 5000);
    ASSERT_EQ(timed_out, 1);
    ASSERT_TRUE(has_text);
#endif
    TEST_END();
}
