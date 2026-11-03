#include "test_framework.h"
#include "telnet_proto.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char data[1024];
    size_t data_len;
    unsigned char reply[4096];
    size_t reply_len;
} TnOut;

static void rx(TnProto *p, const unsigned char *in, size_t len, TnOut *o)
{
    o->data_len = 0;
    o->reply_len = 0;
    tn_proto_rx(p, in, len, o->data, &o->data_len, o->reply, sizeof(o->reply), &o->reply_len);
}

#define RX(p, o, ...) do { \
        static const unsigned char _b[] = { __VA_ARGS__ }; \
        rx((p), _b, sizeof(_b), (o)); \
    } while (0)

#define REPLY_IS(o, ...) do { \
        static const unsigned char _e[] = { __VA_ARGS__ }; \
        ASSERT_EQ((int)(o)->reply_len, (int)sizeof(_e)); \
        ASSERT_TRUE(memcmp((o)->reply, _e, sizeof(_e)) == 0); \
    } while (0)

int test_tn_proto_plain_data_passes_through(void)
{
    TEST_BEGIN();
    TnProto p; TnOut o;
    tn_proto_init(&p, 80, 24);
    rx(&p, (const unsigned char *)"hello\r\n", 7, &o);
    ASSERT_EQ((int)o.data_len, 7);
    ASSERT_TRUE(memcmp(o.data, "hello\r\n", 7) == 0);
    ASSERT_EQ((int)o.reply_len, 0);
    TEST_END();
}

int test_tn_proto_answers_will_echo_and_sga(void)
{
    TEST_BEGIN();
    TnProto p; TnOut o;
    tn_proto_init(&p, 80, 24);
    RX(&p, &o, 0xFF, 0xFB, 0x01, 0xFF, 0xFB, 0x03);
    REPLY_IS(&o, 0xFF, 0xFD, 0x01, 0xFF, 0xFD, 0x03);
    ASSERT_EQ(tn_proto_server_echoes(&p), 1);
    RX(&p, &o, 0xFF, 0xFB, 0x01, 0xFF, 0xFB, 0x03);
    ASSERT_EQ((int)o.reply_len, 0);
    TEST_END();
}

int test_tn_proto_wont_echo_resumes_local_echo(void)
{
    TEST_BEGIN();
    TnProto p; TnOut o;
    tn_proto_init(&p, 80, 24);
    RX(&p, &o, 0xFF, 0xFB, 0x01);
    ASSERT_EQ(tn_proto_server_echoes(&p), 1);
    RX(&p, &o, 0xFF, 0xFC, 0x01);
    REPLY_IS(&o, 0xFF, 0xFE, 0x01);
    ASSERT_EQ(tn_proto_server_echoes(&p), 0);
    RX(&p, &o, 0xFF, 0xFC, 0x01);
    ASSERT_EQ((int)o.reply_len, 0);
    TEST_END();
}

int test_tn_proto_do_sga_and_ttype_send(void)
{
    TEST_BEGIN();
    TnProto p; TnOut o;
    tn_proto_init(&p, 80, 24);
    RX(&p, &o, 0xFF, 0xFD, 0x03);
    REPLY_IS(&o, 0xFF, 0xFB, 0x03);
    RX(&p, &o, 0xFF, 0xFD, 0x18);
    REPLY_IS(&o, 0xFF, 0xFB, 0x18);
    RX(&p, &o, 0xFF, 0xFA, 0x18, 0x01, 0xFF, 0xF0);
    REPLY_IS(&o, 0xFF, 0xFA, 0x18, 0x00, 0x58, 0x54, 0x45, 0x52, 0x4D, 0xFF, 0xF0);
    TEST_END();
}

int test_tn_proto_ttype_send_before_do_is_ignored(void)
{
    TEST_BEGIN();
    TnProto p; TnOut o;
    tn_proto_init(&p, 80, 24);
    RX(&p, &o, 0xFF, 0xFA, 0x18, 0x01, 0xFF, 0xF0);
    ASSERT_EQ((int)o.reply_len, 0);
    TEST_END();
}

int test_tn_proto_do_naws_sends_size(void)
{
    TEST_BEGIN();
    TnProto p; TnOut o;
    tn_proto_init(&p, 80, 24);
    RX(&p, &o, 0xFF, 0xFD, 0x1F);
    REPLY_IS(&o, 0xFF, 0xFB, 0x1F, 0xFF, 0xFA, 0x1F, 0x00, 0x50, 0x00, 0x18, 0xFF, 0xF0);
    RX(&p, &o, 0xFF, 0xFD, 0x1F);
    ASSERT_EQ((int)o.reply_len, 0);
    TEST_END();
}

int test_tn_proto_naws_resize_only_when_agreed(void)
{
    TEST_BEGIN();
    TnProto p; TnOut o;
    unsigned char out[16];
    static const unsigned char want[] = { 0xFF, 0xFA, 0x1F, 0x00, 0x64, 0x00, 0x28, 0xFF, 0xF0 };
    tn_proto_init(&p, 80, 24);
    ASSERT_EQ((int)tn_proto_resize(&p, 100, 40, out, sizeof(out)), 0);
    RX(&p, &o, 0xFF, 0xFD, 0x1F);
    ASSERT_EQ((int)tn_proto_resize(&p, 100, 40, out, sizeof(out)), (int)sizeof(want));
    ASSERT_TRUE(memcmp(out, want, sizeof(want)) == 0);
    TEST_END();
}

int test_tn_proto_naws_doubles_ff_bytes(void)
{
    TEST_BEGIN();
    TnProto p; TnOut o;
    unsigned char out[16];
    static const unsigned char want[] = { 0xFF, 0xFA, 0x1F, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xF0 };
    tn_proto_init(&p, 80, 24);
    RX(&p, &o, 0xFF, 0xFD, 0x1F);
    ASSERT_EQ((int)tn_proto_resize(&p, 255, 255, out, sizeof(out)), (int)sizeof(want));
    ASSERT_TRUE(memcmp(out, want, sizeof(want)) == 0);
    /* too small a buffer writes nothing */
    ASSERT_EQ((int)tn_proto_resize(&p, 255, 255, out, 5), 0);
    TEST_END();
}

int test_tn_proto_refuses_other_options(void)
{
    TEST_BEGIN();
    TnProto p; TnOut o;
    tn_proto_init(&p, 80, 24);
    RX(&p, &o, 0xFF, 0xFB, 0x05);
    REPLY_IS(&o, 0xFF, 0xFE, 0x05);
    RX(&p, &o, 0xFF, 0xFD, 0x01);
    REPLY_IS(&o, 0xFF, 0xFC, 0x01);
    RX(&p, &o, 0xFF, 0xFD, 0x27);
    REPLY_IS(&o, 0xFF, 0xFC, 0x27);
    RX(&p, &o, 0xFF, 0xFC, 0x05);
    ASSERT_EQ((int)o.reply_len, 0);
    RX(&p, &o, 0xFF, 0xFE, 0x05);
    ASSERT_EQ((int)o.reply_len, 0);
    TEST_END();
}

int test_tn_proto_never_sends_unprompted(void)
{
    TEST_BEGIN();
    TnProto p; TnOut o;
    tn_proto_init(&p, 80, 24);
    ASSERT_EQ(tn_proto_server_echoes(&p), 0);
    rx(&p, (const unsigned char *)"220 smtp ready\r\n", 16, &o);
    ASSERT_EQ((int)o.reply_len, 0);
    ASSERT_EQ((int)o.data_len, 16);
    TEST_END();
}

int test_tn_proto_iac_iac_is_data(void)
{
    TEST_BEGIN();
    TnProto p; TnOut o;
    tn_proto_init(&p, 80, 24);
    RX(&p, &o, 0x41, 0xFF, 0xFF, 0x42);
    ASSERT_EQ((int)o.data_len, 3);
    ASSERT_TRUE(o.data[0] == 0x41 && o.data[1] == 0xFF && o.data[2] == 0x42);
    TEST_END();
}

int test_tn_proto_strips_other_commands(void)
{
    TEST_BEGIN();
    TnProto p; TnOut o;
    tn_proto_init(&p, 80, 24);
    RX(&p, &o, 0x41, 0xFF, 0xF1, 0x42, 0xFF, 0xF9, 0x43);
    ASSERT_EQ((int)o.data_len, 3);
    ASSERT_TRUE(memcmp(o.data, "ABC", 3) == 0);
    ASSERT_EQ((int)o.reply_len, 0);
    TEST_END();
}

int test_tn_proto_split_at_every_offset_equals_whole(void)
{
    TEST_BEGIN();
    static const unsigned char in[] = {
        'x', 0xFF, 0xFB, 0x01, 'y', 0xFF, 0xFA, 0x18, 0x01, 0xFF, 0xF0,
        0xFF, 0xFF, 'z', 0xFF, 0xFD, 0x1F
    };
    size_t n = sizeof(in);
    TnProto p; TnOut whole, a, b;
    tn_proto_init(&p, 80, 24);
    RX(&p, &a, 0xFF, 0xFD, 0x18);
    rx(&p, in, n, &whole);
    ASSERT_TRUE(whole.reply_len > 0);
    for (size_t k = 0; k <= n; k++) {
        TnProto q;
        tn_proto_init(&q, 80, 24);
        RX(&q, &a, 0xFF, 0xFD, 0x18);
        rx(&q, in, k, &a);
        rx(&q, in + k, n - k, &b);
        ASSERT_EQ((int)(a.data_len + b.data_len), (int)whole.data_len);
        ASSERT_TRUE(memcmp(a.data, whole.data, a.data_len) == 0);
        ASSERT_TRUE(memcmp(b.data, whole.data + a.data_len, b.data_len) == 0);
        ASSERT_EQ((int)(a.reply_len + b.reply_len), (int)whole.reply_len);
        ASSERT_TRUE(memcmp(a.reply, whole.reply, a.reply_len) == 0);
        ASSERT_TRUE(memcmp(b.reply, whole.reply + a.reply_len, b.reply_len) == 0);
    }
    TEST_END();
}

int test_tn_proto_long_subnegotiation_is_ignored(void)
{
    TEST_BEGIN();
    TnProto p; TnOut o;
    unsigned char in[3 + 100 + 2 + 2];
    in[0] = 0xFF; in[1] = 0xFA; in[2] = 0x18;
    memset(in + 3, 'a', 100);
    in[103] = 0xFF; in[104] = 0xF0; in[105] = 'o'; in[106] = 'k';
    tn_proto_init(&p, 80, 24);
    RX(&p, &o, 0xFF, 0xFD, 0x18);
    rx(&p, in, sizeof(in), &o);
    ASSERT_EQ((int)o.data_len, 2);
    ASSERT_TRUE(memcmp(o.data, "ok", 2) == 0);
    ASSERT_EQ((int)o.reply_len, 0);
    TEST_END();
}

int test_tn_proto_random_bytes_are_bounded(void)
{
    TEST_BEGIN();
    unsigned long seed = 12345u;
    static const unsigned char bias[] = { 0xFF, 0xFA, 0xF0, 0xFD, 0xFB, 0x1F, 0x18, 0x01 };
    for (int iter = 0; iter < 2000; iter++) {
        unsigned char in[512], data[512];
        TnProto p;
        size_t len, dl = 0, rl = 0, cap;
        unsigned char *reply;
        seed = seed * 1103515245u + 12345u;
        len = (size_t)((seed >> 16) % 513u);
        for (size_t i = 0; i < len; i++) {
            seed = seed * 1103515245u + 12345u;
            unsigned r = (unsigned)(seed >> 16);
            in[i] = (r & 1u) ? bias[(r >> 1) % sizeof(bias)] : (unsigned char)(r >> 4);
        }
        cap = TN_REPLY_CAP(len);
        reply = (unsigned char *)malloc(cap);
        ASSERT_NOT_NULL(reply);
        tn_proto_init(&p, 80, 24);
        tn_proto_rx(&p, in, len, data, &dl, reply, cap, &rl);
        ASSERT_TRUE(dl <= len);
        ASSERT_TRUE(rl <= cap);
        free(reply);
    }
    TEST_END();
}

int test_tn_proto_reply_cap_is_enforced(void)
{
    TEST_BEGIN();
    TnProto p;
    unsigned char data[16], reply[2];
    size_t dl = 0, rl = 0;
    static const unsigned char in[] = { 0xFF, 0xFB, 0x01 };
    tn_proto_init(&p, 80, 24);
    tn_proto_rx(&p, in, sizeof(in), data, &dl, reply, sizeof(reply), &rl);
    ASSERT_TRUE(rl <= sizeof(reply));
    TEST_END();
}

int test_tn_proto_encode_escapes_and_expands(void)
{
    TEST_BEGIN();
    TnProto p;
    unsigned char out[32];
    static const unsigned char a[] = { 'a', 0xFF, 'b', '\r' };
    static const unsigned char wa[] = { 0x61, 0xFF, 0xFF, 0x62, 0x0D, 0x0A };
    size_t n;
    tn_proto_init(&p, 80, 24);
    n = tn_proto_encode(&p, a, sizeof(a), out, sizeof(out));
    ASSERT_EQ((int)n, (int)sizeof(wa));
    ASSERT_TRUE(memcmp(out, wa, n) == 0);
    tn_proto_init(&p, 80, 24);
    n = tn_proto_encode(&p, (const unsigned char *)"a\r\nb", 4, out, sizeof(out));
    ASSERT_EQ((int)n, 4);
    ASSERT_TRUE(memcmp(out, "a\r\nb", 4) == 0);
    TEST_END();
}

int test_tn_proto_encode_cr_lf_not_doubled_across_writes(void)
{
    TEST_BEGIN();
    TnProto p;
    unsigned char out[32];
    size_t n;
    tn_proto_init(&p, 80, 24);
    n = tn_proto_encode(&p, (const unsigned char *)"ls\r", 3, out, sizeof(out));
    ASSERT_EQ((int)n, 4);
    ASSERT_TRUE(memcmp(out, "ls\r\n", 4) == 0);
    n = tn_proto_encode(&p, (const unsigned char *)"\n", 1, out, sizeof(out));
    ASSERT_EQ((int)n, 0);
    tn_proto_init(&p, 80, 24);
    n = tn_proto_encode(&p, (const unsigned char *)"x\r", 2, out, sizeof(out));
    ASSERT_EQ((int)n, 3);
    ASSERT_TRUE(memcmp(out, "x\r\n", 3) == 0);
    n = tn_proto_encode(&p, (const unsigned char *)"y", 1, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    ASSERT_TRUE(out[0] == 'y');
    /* a CR LF the caller sent whole does not arm the drop */
    n = tn_proto_encode(&p, (const unsigned char *)"\r\n", 2, out, sizeof(out));
    ASSERT_EQ((int)n, 2);
    n = tn_proto_encode(&p, (const unsigned char *)"\n", 1, out, sizeof(out));
    ASSERT_EQ((int)n, 1);
    TEST_END();
}
