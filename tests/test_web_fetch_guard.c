#include "test_framework.h"
#include "ai_tool_web_fetch.h"
#include "net_guard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A scripted backend for the web_fetch hop loop: a host-to-address table, a
 * list of responses consumed one per get (the last one repeats), a clock that
 * every get advances, and a record of every call the loop made. */

#define FK_MAX_PAIRS 64
#define FK_MAX_RESP  16
#define FK_MAX_CALLS 16

typedef struct {
    char    host[NET_HOST_MAX];
    NetAddr addr;
} FkPair;

typedef struct {
    int         status;
    const char *location;
    const char *body;
} FkResp;

typedef struct {
    FkPair pairs[FK_MAX_PAIRS];
    int    npairs;

    FkResp resp[FK_MAX_RESP];
    int    nresp;
    int    get_fail;        /* the get returns -1 instead of a response */
    int    cancel_in_get;   /* the get raises the cancel flag */

    int    force_resolve;   /* resolve returns force_ret whatever the host */
    int    force_ret;

    volatile int *cancel_target;   /* flag the hooks below raise */
    int    cancel_in_resolve;      /* resolve raises *cancel_target */
    int    cancel_in_hop;          /* on_hop raises *cancel_target */
    unsigned long long hop_jump;   /* on_hop adds this to the clock */

    unsigned long long now;
    unsigned long long step; /* added to the clock by every get */

    int         resolves;
    char        resolved[FK_MAX_CALLS][NET_HOST_MAX];
    int         gets;
    NetUrlParts got[FK_MAX_CALLS];
    int         got_timeout[FK_MAX_CALLS];

    int  ends;               /* times the backend's end hook ran */

    int  hops;
    char hop_url[FK_MAX_CALLS][NET_URL_DISPLAY_MAX + 8];
    int  hop_refused[FK_MAX_CALLS];
} Fake;

static void fk_host(Fake *f, const char *host, int a, int b, int c, int d)
{
    FkPair *p = &f->pairs[f->npairs++];
    snprintf(p->host, sizeof(p->host), "%s", host);
    memset(&p->addr, 0, sizeof(p->addr));
    p->addr.b[0] = (unsigned char)a;
    p->addr.b[1] = (unsigned char)b;
    p->addr.b[2] = (unsigned char)c;
    p->addr.b[3] = (unsigned char)d;
}

static void fk_resp(Fake *f, int status, const char *location, const char *body)
{
    FkResp *r = &f->resp[f->nresp++];
    r->status   = status;
    r->location = location;
    r->body     = body;
}

static int fk_resolve(void *ctx, const char *host, NetAddr *out, int max)
{
    Fake *f = (Fake *)ctx;
    if (f->resolves < FK_MAX_CALLS)
        snprintf(f->resolved[f->resolves], NET_HOST_MAX, "%s", host);
    f->resolves++;
    if (f->cancel_in_resolve && f->cancel_target) *f->cancel_target = 1;
    if (f->force_resolve) return f->force_ret;

    int total = 0;
    for (int i = 0; i < f->npairs; i++) {
        if (strcmp(f->pairs[i].host, host) != 0) continue;
        if (total < max) out[total] = f->pairs[i].addr;
        total++;
    }
    return total > 0 ? total : -1;
}

static int fk_get(void *ctx, const NetUrlParts *url, int timeout_ms,
                  volatile int *cancel, int *status,
                  char *location, size_t location_cap,
                  char **body, size_t *body_len)
{
    Fake *f = (Fake *)ctx;
    if (f->gets < FK_MAX_CALLS) {
        f->got[f->gets]         = *url;
        f->got_timeout[f->gets] = timeout_ms;
    }
    int idx = f->gets;
    f->gets++;
    f->now += f->step;
    if (f->cancel_in_get && cancel) *cancel = 1;
    if (f->get_fail || f->nresp == 0) return -1;

    const FkResp *r = &f->resp[idx < f->nresp ? idx : f->nresp - 1];
    *status = r->status;
    snprintf(location, location_cap, "%s", r->location ? r->location : "");
    const char *text = r->body ? r->body : "";
    *body_len = strlen(text);
    *body = malloc(*body_len + 1);
    if (!*body) return -1;
    memcpy(*body, text, *body_len + 1);
    return 0;
}

static unsigned long long fk_now(void *ctx)
{
    return ((Fake *)ctx)->now;
}

static void fk_end(void *ctx)
{
    ((Fake *)ctx)->ends++;
}

static void fk_on_hop(void *user, const char *display_url, int refused)
{
    Fake *f = (Fake *)user;
    f->now += f->hop_jump;
    if (f->cancel_in_hop && f->cancel_target) *f->cancel_target = 1;
    if (f->hops < FK_MAX_CALLS) {
        snprintf(f->hop_url[f->hops], sizeof(f->hop_url[0]), "%s", display_url);
        f->hop_refused[f->hops] = refused;
    }
    f->hops++;
}

static void fk_init(Fake *f, WebFetchBackend *be, WebFetchContext *ctx)
{
    memset(f, 0, sizeof(*f));
    memset(be, 0, sizeof(*be));
    memset(ctx, 0, sizeof(*ctx));
    be->ctx     = f;
    be->resolve = fk_resolve;
    be->get     = fk_get;
    be->now_ms  = fk_now;
    be->end     = fk_end;
    ctx->timeout_ms = 10000;
    ctx->on_hop     = fk_on_hop;
    ctx->user       = f;
    ctx->backend    = be;
}

/* Runs the tool on url; the caller frees *buf. */
static int fk_run(WebFetchContext *ctx, const char *url, volatile int *cancel,
                  char **buf)
{
    char   json[4400];
    size_t len = 0;
    int    trunc = 0;
    snprintf(json, sizeof(json), "{\"url\":\"%s\"}", url);
    *buf = NULL;
    return tool_web_fetch_execute(json, ctx, cancel, buf, &len, &trunc);
}

int test_web_fetch_success_converts_html(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>Hi</p>");
    char *buf;
    int rc = fk_run(&ctx, "http://pub.test/", NULL, &buf);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_STR_EQ(buf, "Hi");
    ASSERT_EQ(f.gets, 1);
    ASSERT_EQ(f.hops, 1);
    ASSERT_STR_EQ(f.hop_url[0], "http://pub.test/");
    ASSERT_EQ(f.hop_refused[0], 0);
    free(buf);
    TEST_END();
}

int test_web_fetch_refuses_loopback_first_hop(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "loop.test", 127, 0, 0, 1);
    fk_resp(&f, 200, NULL, "<p>secret</p>");
    char *buf;
    int rc = fk_run(&ctx, "http://loop.test/", NULL, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "local or private") != NULL);
    ASSERT_EQ(f.gets, 0);
    ASSERT_EQ(f.hops, 1);
    ASSERT_EQ(f.hop_refused[0], 1);
    free(buf);
    TEST_END();
}

int test_web_fetch_refuses_if_any_address_internal(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "mixed.test", 8, 8, 8, 8);
    fk_host(&f, "mixed.test", 10, 0, 0, 1);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    char *buf;
    int rc = fk_run(&ctx, "http://mixed.test/", NULL, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_EQ(f.gets, 0);
    ASSERT_EQ(f.hop_refused[0], 1);
    free(buf);
    TEST_END();
}

int test_web_fetch_refuses_when_resolve_fails(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    char *buf;
    int rc = fk_run(&ctx, "http://nowhere.test/", NULL, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "resolve") != NULL);
    ASSERT_EQ(f.gets, 0);
    ASSERT_EQ(f.hops, 0);
    free(buf);

    /* A resolver that reports success with no addresses is refused too. */
    fk_init(&f, &be, &ctx);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    f.force_resolve = 1;
    f.force_ret     = 0;
    rc = fk_run(&ctx, "http://empty.test/", NULL, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_EQ(f.gets, 0);
    free(buf);
    TEST_END();
}

int test_web_fetch_refuses_too_many_addresses(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    for (int i = 0; i < WEB_FETCH_MAX_ADDRS + 1; i++)
        fk_host(&f, "many.test", 8, 8, 8, (unsigned char)i);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    char *buf;
    int rc = fk_run(&ctx, "http://many.test/", NULL, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "too many addresses") != NULL);
    ASSERT_EQ(f.gets, 0);
    free(buf);

    /* Exactly the maximum is accepted. */
    fk_init(&f, &be, &ctx);
    for (int i = 0; i < WEB_FETCH_MAX_ADDRS; i++)
        fk_host(&f, "many.test", 8, 8, 8, (unsigned char)i);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    rc = fk_run(&ctx, "http://many.test/", NULL, &buf);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(f.gets, 1);
    free(buf);
    TEST_END();
}

int test_web_fetch_refuses_internal_at_later_hop(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_host(&f, "intra.test", 192, 168, 1, 1);
    fk_resp(&f, 302, "http://intra.test/", "<p>moved</p>");
    fk_resp(&f, 200, NULL, "<p>internal secret</p>");
    char *buf;
    int rc = fk_run(&ctx, "http://pub.test/", NULL, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "local or private") != NULL);
    if (buf) ASSERT_TRUE(strstr(buf, "internal secret") == NULL);
    ASSERT_EQ(f.gets, 1);
    ASSERT_EQ(f.hops, 2);
    ASSERT_STR_EQ(f.hop_url[0], "http://pub.test/");
    ASSERT_EQ(f.hop_refused[0], 0);
    ASSERT_STR_EQ(f.hop_url[1], "http://intra.test/");
    ASSERT_EQ(f.hop_refused[1], 1);
    free(buf);
    TEST_END();
}

int test_web_fetch_follows_five_redirects(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    for (int i = 0; i < 5; i++) fk_resp(&f, 302, "http://pub.test/next", "");
    fk_resp(&f, 200, NULL, "<p>done</p>");
    char *buf;
    int rc = fk_run(&ctx, "http://pub.test/", NULL, &buf);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(f.gets, 6);
    ASSERT_EQ(f.resolves, 6);
    if (buf) ASSERT_STR_EQ(buf, "done");
    free(buf);
    TEST_END();
}

int test_web_fetch_sixth_redirect_fails(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 302, "http://pub.test/next", "<p>loop</p>");
    char *buf;
    int rc = fk_run(&ctx, "http://pub.test/", NULL, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "too many redirects") != NULL);
    ASSERT_EQ(f.gets, 6);
    free(buf);
    TEST_END();
}

int test_web_fetch_redirect_statuses(void)
{
    TEST_BEGIN();
    static const int follow[] = { 301, 302, 303, 307, 308 };
    for (size_t i = 0; i < sizeof(follow) / sizeof(follow[0]); i++) {
        Fake f; WebFetchBackend be; WebFetchContext ctx;
        fk_init(&f, &be, &ctx);
        fk_host(&f, "pub.test", 93, 184, 216, 34);
        fk_resp(&f, follow[i], "/there", "");
        fk_resp(&f, 200, NULL, "<p>there</p>");
        char *buf;
        int rc = fk_run(&ctx, "http://pub.test/", NULL, &buf);
        ASSERT_EQ(rc, 0);
        ASSERT_EQ(f.gets, 2);
        if (f.gets == 2) ASSERT_STR_EQ(f.got[1].path, "/there");
        free(buf);
    }
    static const int final_status[] = { 300, 304 };
    for (size_t i = 0; i < sizeof(final_status) / sizeof(final_status[0]); i++) {
        Fake f; WebFetchBackend be; WebFetchContext ctx;
        fk_init(&f, &be, &ctx);
        fk_host(&f, "pub.test", 93, 184, 216, 34);
        fk_resp(&f, final_status[i], "/there", "<p>choices</p>");
        char *buf;
        int rc = fk_run(&ctx, "http://pub.test/", NULL, &buf);
        ASSERT_EQ(rc, 0);
        ASSERT_EQ(f.gets, 1);
        if (buf) ASSERT_STR_EQ(buf, "choices");
        free(buf);
    }
    TEST_END();
}

int test_web_fetch_deadline(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 302, "http://pub.test/again", "<p>leak</p>");
    f.step = 11000;
    char *buf;
    int rc = fk_run(&ctx, "http://pub.test/", NULL, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "timed out") != NULL);
    if (buf) ASSERT_TRUE(strstr(buf, "leak") == NULL);
    ASSERT_EQ(f.gets, 3);
    ASSERT_EQ(f.got_timeout[0], 10000);
    ASSERT_EQ(f.got_timeout[1], 10000);
    ASSERT_EQ(f.got_timeout[2], 8000);
    free(buf);
    TEST_END();
}

int test_web_fetch_per_hop_timeout_from_context(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    ctx.timeout_ms = 2500;
    char *buf;
    ASSERT_EQ(fk_run(&ctx, "http://pub.test/", NULL, &buf), 0);
    ASSERT_EQ(f.got_timeout[0], 2500);
    free(buf);

    /* A non-positive setting falls back to 10 s. */
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    ctx.timeout_ms = 0;
    ASSERT_EQ(fk_run(&ctx, "http://pub.test/", NULL, &buf), 0);
    ASSERT_EQ(f.got_timeout[0], 10000);
    free(buf);
    TEST_END();
}

int test_web_fetch_cancel_before_first_hop(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    volatile int cancel = 1;
    char *buf;
    int rc = fk_run(&ctx, "http://pub.test/", &cancel, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "cancelled") != NULL);
    ASSERT_EQ(f.resolves, 0);
    ASSERT_EQ(f.gets, 0);
    free(buf);
    TEST_END();
}

int test_web_fetch_cancel_during_get(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>secret</p>");
    f.cancel_in_get = 1;
    volatile int cancel = 0;
    char *buf;
    int rc = fk_run(&ctx, "http://pub.test/", &cancel, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "cancelled") != NULL);
    if (buf) ASSERT_TRUE(strstr(buf, "secret") == NULL);
    ASSERT_EQ(f.gets, 1);
    free(buf);
    TEST_END();
}

int test_web_fetch_https_to_http_refused(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 301, "http://pub.test/", "<p>x</p>");
    char *buf;
    int rc = fk_run(&ctx, "https://pub.test/", NULL, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "https to http") != NULL);
    ASSERT_EQ(f.gets, 1);
    free(buf);

    /* http to https is fine. */
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 301, "https://pub.test/", "");
    fk_resp(&f, 200, NULL, "<p>secure</p>");
    rc = fk_run(&ctx, "http://pub.test/", NULL, &buf);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(f.gets, 2);
    ASSERT_EQ(f.got[1].https, 1);
    ASSERT_EQ((int)f.got[1].port, 443);
    free(buf);
    TEST_END();
}

int test_web_fetch_relative_locations(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_host(&f, "other.test", 93, 184, 216, 35);
    fk_resp(&f, 302, "c", "");
    fk_resp(&f, 302, "//other.test/p", "");
    fk_resp(&f, 200, NULL, "<p>end</p>");
    char *buf;
    int rc = fk_run(&ctx, "http://pub.test/a/b", NULL, &buf);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(f.gets, 3);
    ASSERT_STR_EQ(f.got[1].host, "pub.test");
    ASSERT_STR_EQ(f.got[1].path, "/a/c");
    ASSERT_STR_EQ(f.resolved[2], "other.test");
    ASSERT_STR_EQ(f.got[2].host, "other.test");
    ASSERT_STR_EQ(f.got[2].path, "/p");
    free(buf);
    TEST_END();
}

int test_web_fetch_bad_location_refused(void)
{
    TEST_BEGIN();
    static const char *const bad[] = { "", "file:///etc/passwd" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        Fake f; WebFetchBackend be; WebFetchContext ctx;
        fk_init(&f, &be, &ctx);
        fk_host(&f, "pub.test", 93, 184, 216, 34);
        fk_resp(&f, 302, bad[i], "<p>x</p>");
        char *buf;
        int rc = fk_run(&ctx, "http://pub.test/", NULL, &buf);
        ASSERT_EQ(rc, -1);
        ASSERT_NOT_NULL(buf);
        if (buf) ASSERT_TRUE(strstr(buf, "redirect") != NULL);
        ASSERT_EQ(f.gets, 1);
        free(buf);
    }
    TEST_END();
}

int test_web_fetch_passes_port(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    char *buf;
    int rc = fk_run(&ctx, "http://pub.test:8080/x", NULL, &buf);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(f.gets, 1);
    ASSERT_EQ((int)f.got[0].port, 8080);
    ASSERT_STR_EQ(f.got[0].path, "/x");
    ASSERT_STR_EQ(f.got[0].host, "pub.test");
    ASSERT_EQ(f.got[0].https, 0);
    free(buf);
    TEST_END();
}

int test_web_fetch_on_hop_hides_query_and_userinfo(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    char *buf;
    int rc = fk_run(&ctx, "https://u:pw@pub.test/p?key=secret", NULL, &buf);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(f.hops, 1);
    ASSERT_STR_EQ(f.hop_url[0], "https://pub.test/p");
    free(buf);
    TEST_END();
}

int test_web_fetch_url_too_long_refused(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    char url[3100];
    snprintf(url, sizeof(url), "http://pub.test/");
    size_t n = strlen(url);
    while (n < 3000) url[n++] = 'a';
    url[n] = '\0';
    char *buf;
    int rc = fk_run(&ctx, url, NULL, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    ASSERT_EQ(f.resolves, 0);
    ASSERT_EQ(f.gets, 0);
    free(buf);
    TEST_END();
}

int test_web_fetch_describe(void)
{
    TEST_BEGIN();
    char out[NET_URL_DISPLAY_MAX + 8];
    ASSERT_EQ(web_fetch_describe("{\"url\":\"https://a.test/p?k=v\"}", out, sizeof(out)), 1);
    ASSERT_STR_EQ(out, "https://a.test/p");
    ASSERT_EQ(web_fetch_describe("{}", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "");
    ASSERT_EQ(web_fetch_describe("not json", out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "");
    ASSERT_EQ(web_fetch_describe(NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "");
    TEST_END();
}

int test_web_fetch_deadline_passed_inside_on_hop(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>late</p>");
    /* A slow row callback carries the clock past the deadline: the hop must
     * not start, and must not be handed a wrapped or zero timeout. */
    f.hop_jump = WEB_FETCH_TOTAL_MS + 1000;
    char *buf;
    int rc = fk_run(&ctx, "http://pub.test/", NULL, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "timed out") != NULL);
    if (buf) ASSERT_TRUE(strstr(buf, "late") == NULL);
    ASSERT_EQ(f.gets, 0);
    free(buf);

    /* Landing exactly on the deadline is out of time too (a zero timeout
     * would mean "wait forever" to WinHTTP). */
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>late</p>");
    f.hop_jump = WEB_FETCH_TOTAL_MS;
    rc = fk_run(&ctx, "http://pub.test/", NULL, &buf);
    ASSERT_EQ(rc, -1);
    if (buf) ASSERT_TRUE(strstr(buf, "timed out") != NULL);
    ASSERT_EQ(f.gets, 0);
    free(buf);

    /* One millisecond left still fetches, with a 1 ms timeout. */
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>just in time</p>");
    f.hop_jump = WEB_FETCH_TOTAL_MS - 1;
    rc = fk_run(&ctx, "http://pub.test/", NULL, &buf);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(f.gets, 1);
    ASSERT_EQ(f.got_timeout[0], 1);
    free(buf);
    TEST_END();
}

int test_web_fetch_get_failure(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    f.get_fail = 1;
    char *buf;
    int rc = fk_run(&ctx, "http://pub.test/", NULL, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "HTTP request failed") != NULL);
    ASSERT_EQ(f.gets, 1);
    free(buf);
    TEST_END();
}

int test_web_fetch_cancel_after_resolve(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    volatile int cancel = 0;
    char *buf;

    /* Raised inside resolve. */
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>secret</p>");
    f.cancel_target     = &cancel;
    f.cancel_in_resolve = 1;
    int rc = fk_run(&ctx, "http://pub.test/", &cancel, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "cancelled") != NULL);
    ASSERT_EQ(f.resolves, 1);
    ASSERT_EQ(f.gets, 0);
    free(buf);

    /* Raised inside the row callback, the last step before the request. */
    cancel = 0;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>secret</p>");
    f.cancel_target = &cancel;
    f.cancel_in_hop = 1;
    rc = fk_run(&ctx, "http://pub.test/", &cancel, &buf);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    if (buf) ASSERT_TRUE(strstr(buf, "cancelled") != NULL);
    ASSERT_EQ(f.gets, 0);
    free(buf);
    TEST_END();
}

int test_web_fetch_end_hook_runs_once_on_every_exit(void)
{
    TEST_BEGIN();
    Fake f; WebFetchBackend be; WebFetchContext ctx;
    char *buf;
    volatile int cancel = 0;

    /* success, after redirects: one end for the whole fetch, not one per hop */
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 302, "http://pub.test/b", "");
    fk_resp(&f, 302, "http://pub.test/c", "");
    fk_resp(&f, 200, NULL, "<p>x</p>");
    ASSERT_EQ(fk_run(&ctx, "http://pub.test/a", NULL, &buf), 0);
    ASSERT_EQ(f.gets, 3);
    ASSERT_EQ(f.ends, 1);
    free(buf);

    /* refusal: a local address */
    fk_init(&f, &be, &ctx);
    fk_host(&f, "local.test", 127, 0, 0, 1);
    ASSERT_EQ(fk_run(&ctx, "http://local.test/", NULL, &buf), -1);
    ASSERT_EQ(f.ends, 1);
    free(buf);

    /* refusal: an invalid redirect target */
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 302, "ftp://pub.test/x", "");
    ASSERT_EQ(fk_run(&ctx, "http://pub.test/", NULL, &buf), -1);
    ASSERT_EQ(f.ends, 1);
    free(buf);

    /* timeout */
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 302, "http://pub.test/again", "");
    f.step = 11000;
    ASSERT_EQ(fk_run(&ctx, "http://pub.test/", NULL, &buf), -1);
    ASSERT_EQ(f.ends, 1);
    free(buf);

    /* cancel before the first hop, and during a get */
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    cancel = 1;
    ASSERT_EQ(fk_run(&ctx, "http://pub.test/", &cancel, &buf), -1);
    ASSERT_EQ(f.ends, 1);
    free(buf);

    cancel = 0;
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    f.cancel_in_get = 1;
    ASSERT_EQ(fk_run(&ctx, "http://pub.test/", &cancel, &buf), -1);
    ASSERT_EQ(f.ends, 1);
    free(buf);

    /* a failed request */
    fk_init(&f, &be, &ctx);
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    f.get_fail = 1;
    ASSERT_EQ(fk_run(&ctx, "http://pub.test/", NULL, &buf), -1);
    ASSERT_EQ(f.ends, 1);
    free(buf);

    /* a backend with no end hook is fine */
    fk_init(&f, &be, &ctx);
    be.end = NULL;
    fk_host(&f, "pub.test", 93, 184, 216, 34);
    fk_resp(&f, 200, NULL, "<p>x</p>");
    ASSERT_EQ(fk_run(&ctx, "http://pub.test/", NULL, &buf), 0);
    ASSERT_EQ(f.ends, 0);
    free(buf);
    TEST_END();
}
