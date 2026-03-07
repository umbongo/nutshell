#include "test_framework.h"
#include "ssh_session.h"
#include "ssh_channel.h"
#include "ssh_pty.h"
#include "ssh_io.h"

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
     * Resizing to (0, 0) should be a no-op (dedup fires), not a libssh2 call.
     * Resizing to any real size with a NULL channel must return -1. */
    SSHChannel ch;
    ch.channel   = NULL;
    ch.ssh       = NULL;
    ch.last_cols = 0;
    ch.last_rows = 0;

    ASSERT_EQ(ssh_pty_resize(&ch, 0, 0), 0);   /* dedup – same as initial */
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