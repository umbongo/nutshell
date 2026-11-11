#include "test_framework.h"
#include "run_command.h"
#include "ai_prompt.h"

#include <stdlib.h>
#include <string.h>

/* A valid UTF-8 string with no partial sequence at its end. */
static int utf8_whole(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n = (*p < 0x80u) ? 1 : ((*p & 0xE0u) == 0xC0u) ? 2
              : ((*p & 0xF0u) == 0xE0u) ? 3 : ((*p & 0xF8u) == 0xF0u) ? 4 : 0;
        if (n == 0) return 0;
        for (int i = 1; i < n; i++)
            if ((p[i] & 0xC0u) != 0x80u) return 0;
        p += n;
    }
    return 1;
}

int test_runcmd_parse_ok(void)
{
    TEST_BEGIN();
    char cmd[RUNCMD_COMMAND_MAX + 1];
    char note[RUNCMD_NOTE_MAX];

    ASSERT_EQ(runcmd_parse_input("{\"command\":\"ls -la\",\"note\":\"list\"}", cmd, note),
              RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(cmd, "ls -la");
    ASSERT_STR_EQ(note, "list");

    /* no note: note is "" */
    memset(note, 'x', sizeof note);
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"pwd\"}", cmd, note), RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(cmd, "pwd");
    ASSERT_STR_EQ(note, "");

    /* a note that is not a string: "" and still OK */
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"pwd\",\"note\":5}", cmd, note),
              RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(note, "");

    /* nothing is trimmed: the command is the string as sent */
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"  ls \"}", cmd, note), RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(cmd, "  ls ");

    /* escaped quotes and non-ASCII text survive */
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"echo \\\"h\xC3\xA9\\\"\"}", cmd, note),
              RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(cmd, "echo \"h\xC3\xA9\"");
    TEST_END();
}

int test_runcmd_parse_no_command(void)
{
    TEST_BEGIN();
    char cmd[RUNCMD_COMMAND_MAX + 1];
    char note[RUNCMD_NOTE_MAX];
    const char *bad[] = {
        NULL, "", "{}", "{\"command\":\"\"}", "{\"command\":\"   \"}",
        "{\"command\":\" \\t \"}", "{\"command\":5}", "{\"command\":null}",
        "{\"command\":[\"ls\"]}", "[1]", "\"ls\"", "{\"command\":\"ls",
        "{\"note\":\"only a note\"}"
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memset(cmd, 'x', sizeof cmd);
        memset(note, 'x', sizeof note);
        RunCmdParse p = runcmd_parse_input(bad[i], cmd, note);
        if (p != RUNCMD_PARSE_NO_COMMAND) {
            printf("  case %zu: got %d\n", i, (int)p);
            _tf_local_fail = 1;
        }
        /* both outputs are defined on a failure */
        ASSERT_STR_EQ(cmd, "");
        ASSERT_STR_EQ(note, "");
    }
    TEST_END();
}

int test_runcmd_parse_length(void)
{
    TEST_BEGIN();
    char cmd[RUNCMD_COMMAND_MAX + 1];
    char note[RUNCMD_NOTE_MAX];
    static char json[RUNCMD_COMMAND_MAX + 64];

    memcpy(json, "{\"command\":\"", 12);
    memset(json + 12, 'a', RUNCMD_COMMAND_MAX);
    memcpy(json + 12 + RUNCMD_COMMAND_MAX, "\"}", 3);
    ASSERT_EQ(runcmd_parse_input(json, cmd, note), RUNCMD_PARSE_OK);
    ASSERT_EQ(strlen(cmd), (size_t)RUNCMD_COMMAND_MAX);

    memset(json + 12, 'a', RUNCMD_COMMAND_MAX + 1);
    memcpy(json + 12 + RUNCMD_COMMAND_MAX + 1, "\"}", 3);
    ASSERT_EQ(runcmd_parse_input(json, cmd, note), RUNCMD_PARSE_TOO_LONG);
    ASSERT_STR_EQ(cmd, "");

    /* too long wins over unsafe: the model is told to split it first */
    memset(json + 12, 'a', RUNCMD_COMMAND_MAX + 2);
    memcpy(json + 12 + RUNCMD_COMMAND_MAX + 2, "\"}", 3);
    json[12] = '\\'; json[13] = 'n';   /* 1 newline + 1024 more: 1025 bytes */
    ASSERT_EQ(runcmd_parse_input(json, cmd, note), RUNCMD_PARSE_TOO_LONG);
    TEST_END();
}

int test_runcmd_parse_unsafe(void)
{
    TEST_BEGIN();
    char cmd[RUNCMD_COMMAND_MAX + 1];
    char note[RUNCMD_NOTE_MAX];
    const char *bad[] = {
        "{\"command\":\"ls\\nrm x\"}",
        "{\"command\":\"a\\tb\"}",
        "{\"command\":\"a\\rb\"}",
        "{\"command\":\"a\\u001b[0m\"}",
        "{\"command\":\"a\\u007fb\"}",
        "{\"command\":\"a\\u0085b\"}",
        "{\"command\":\"a\\u202eb\"}",
        "{\"command\":\"a\\u2066b\"}",
        "{\"command\":\"a\xE2\x80\xAEz\"}",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        RunCmdParse p = runcmd_parse_input(bad[i], cmd, note);
        if (p != RUNCMD_PARSE_UNSAFE) {
            printf("  case %zu: got %d\n", i, (int)p);
            _tf_local_fail = 1;
        }
        ASSERT_STR_EQ(cmd, "");
    }
    /* ordinary non-ASCII text is not unsafe */
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"echo \xE2\x82\xAC\"}", cmd, note),
              RUNCMD_PARSE_OK);
    TEST_END();
}

int test_runcmd_parse_note_cut(void)
{
    TEST_BEGIN();
    char cmd[RUNCMD_COMMAND_MAX + 1];
    char note[RUNCMD_NOTE_MAX];
    static char json[1024];

    /* 300 two-byte characters: cut to 255 bytes on a boundary, so 254 */
    strcpy(json, "{\"command\":\"ls\",\"note\":\"");
    size_t at = strlen(json);
    for (int i = 0; i < 300; i++) { json[at++] = (char)0xC3; json[at++] = (char)0xA9; }
    strcpy(json + at, "\"}");
    ASSERT_EQ(runcmd_parse_input(json, cmd, note), RUNCMD_PARSE_OK);
    ASSERT_TRUE(strlen(note) <= 255);
    ASSERT_EQ(strlen(note), (size_t)254);
    ASSERT_TRUE(utf8_whole(note));

    /* three-byte characters: 255 is a multiple of 3, so exactly 255 */
    strcpy(json, "{\"command\":\"ls\",\"note\":\"");
    at = strlen(json);
    for (int i = 0; i < 200; i++) {
        json[at++] = (char)0xE2; json[at++] = (char)0x82; json[at++] = (char)0xAC;
    }
    strcpy(json + at, "\"}");
    ASSERT_EQ(runcmd_parse_input(json, cmd, note), RUNCMD_PARSE_OK);
    ASSERT_EQ(strlen(note), (size_t)255);
    ASSERT_TRUE(utf8_whole(note));

    /* four-byte characters: 63 of them fit (252 bytes) */
    strcpy(json, "{\"command\":\"ls\",\"note\":\"");
    at = strlen(json);
    for (int i = 0; i < 100; i++) {
        json[at++] = (char)0xF0; json[at++] = (char)0x9F;
        json[at++] = (char)0x98; json[at++] = (char)0x80;
    }
    strcpy(json + at, "\"}");
    ASSERT_EQ(runcmd_parse_input(json, cmd, note), RUNCMD_PARSE_OK);
    ASSERT_EQ(strlen(note), (size_t)252);
    ASSERT_TRUE(utf8_whole(note));

    /* an unsafe note is dropped, the command still runs */
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"ls\",\"note\":\"a\\nb\"}", cmd, note),
              RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(cmd, "ls");
    ASSERT_STR_EQ(note, "");
    ASSERT_EQ(runcmd_parse_input("{\"command\":\"ls\",\"note\":\"a\\u202eb\"}", cmd, note),
              RUNCMD_PARSE_OK);
    ASSERT_STR_EQ(note, "");
    TEST_END();
}

int test_runcmd_parse_error_texts(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(runcmd_parse_error(RUNCMD_PARSE_OK), "");
    ASSERT_STR_EQ(runcmd_parse_error(RUNCMD_PARSE_NO_COMMAND),
                  "run_command needs a \"command\" string");
    ASSERT_STR_EQ(runcmd_parse_error(RUNCMD_PARSE_TOO_LONG),
                  "command longer than 1023 bytes: break the work into smaller steps");
    ASSERT_STR_EQ(runcmd_parse_error(RUNCMD_PARSE_UNSAFE),
                  "command contains control or bidirectional formatting characters: "
                  "send one single-line command");
    TEST_END();
}

int test_runcmd_describe(void)
{
    TEST_BEGIN();
    char out[64];

    ASSERT_EQ(runcmd_describe("{\"command\":\"uptime\"}", out, sizeof out), 1);
    ASSERT_STR_EQ(out, "uptime");

    memset(out, 'x', sizeof out);
    ASSERT_EQ(runcmd_describe("{}", out, sizeof out), 0);
    ASSERT_STR_EQ(out, "");

    memset(out, 'x', sizeof out);
    ASSERT_EQ(runcmd_describe("{\"command\":\"a\\nb\"}", out, sizeof out), 0);
    ASSERT_STR_EQ(out, "");

    ASSERT_EQ(runcmd_describe(NULL, out, sizeof out), 0);
    ASSERT_STR_EQ(out, "");

    /* a small buffer: cut on a UTF-8 boundary, still terminated */
    char small[5];
    ASSERT_EQ(runcmd_describe("{\"command\":\"ab\xC3\xA9\xC3\xA9\"}", small, sizeof small), 1);
    ASSERT_STR_EQ(small, "ab\xC3\xA9");

    /* no buffer: nothing written, no crash */
    ASSERT_EQ(runcmd_describe("{\"command\":\"ls\"}", NULL, 0), 1);
    TEST_END();
}

int test_runcmd_auto_default_table(void)
{
    TEST_BEGIN();
    ASSERT_EQ(ai_runcmd_auto_default("anthropic"), 1);
    ASSERT_EQ(ai_runcmd_auto_default("openai"), 1);
    ASSERT_EQ(ai_runcmd_auto_default("moonshot"), 1);
    ASSERT_EQ(ai_runcmd_auto_default("deepseek"), 0);
    ASSERT_EQ(ai_runcmd_auto_default("gemini"), 0);
    ASSERT_EQ(ai_runcmd_auto_default("custom"), 0);
    ASSERT_EQ(ai_runcmd_auto_default("other"), 0);
    ASSERT_EQ(ai_runcmd_auto_default(NULL), 0);
    TEST_END();
}

int test_provider_runcmd_modes(void)
{
    TEST_BEGIN();
    const char *all[] = { "anthropic", "openai", "deepseek", "moonshot", "gemini", "custom" };
    const int web_tools[] = { 1, 1, 1, 1, 0, 0 };   /* ai_provider_tools, AUTO */

    ASSERT_EQ((int)AI_RUNCMD_AUTO, 0);
    ASSERT_EQ((int)AI_RUNCMD_ON, 1);
    ASSERT_EQ((int)AI_RUNCMD_OFF, 2);

    for (int i = 0; i < 6; i++) {
        const char *p = all[i];
        /* Off: no run_command anywhere, the web tools unchanged */
        ASSERT_EQ(ai_provider_runcmd(p, AI_RUNCMD_OFF), 0);
        ASSERT_EQ(ai_provider_tools(p, AI_RUNCMD_OFF), web_tools[i]);
        /* On: every provider */
        ASSERT_EQ(ai_provider_runcmd(p, AI_RUNCMD_ON), 1);
        ASSERT_EQ(ai_provider_tools(p, AI_RUNCMD_ON), 1);
        /* Automatic: the table; web tools as today */
        ASSERT_EQ(ai_provider_runcmd(p, AI_RUNCMD_AUTO), ai_runcmd_auto_default(p));
        ASSERT_EQ(ai_provider_tools(p, AI_RUNCMD_AUTO), web_tools[i]);
        ASSERT_EQ(ai_provider_supports_tools(p), web_tools[i]);
    }

    /* unknown providers and NULL never get tools, whatever the mode */
    for (int m = 0; m <= 2; m++) {
        ASSERT_EQ(ai_provider_runcmd("other", m), 0);
        ASSERT_EQ(ai_provider_runcmd(NULL, m), 0);
        ASSERT_EQ(ai_provider_tools("other", m), 0);
        ASSERT_EQ(ai_provider_tools(NULL, m), 0);
    }
    ASSERT_EQ(ai_provider_supports_tools("other"), 0);
    ASSERT_EQ(ai_provider_supports_tools(NULL), 0);
    TEST_END();
}

int test_runcmd_reject_hint(void)
{
    TEST_BEGIN();
    ASSERT_EQ(runcmd_reject_hint_applies(400, "tools is not supported"), 1);
    ASSERT_EQ(runcmd_reject_hint_applies(422, "Function calling is not enabled"), 1);
    ASSERT_EQ(runcmd_reject_hint_applies(400, "reasoning_content must be passed back"), 1);
    ASSERT_EQ(runcmd_reject_hint_applies(400, "Unknown parameter: TOOLS"), 1);
    ASSERT_EQ(runcmd_reject_hint_applies(400, "REASONING_CONTENT missing"), 1);

    ASSERT_EQ(runcmd_reject_hint_applies(401, "tools"), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(402, "tools"), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(429, "tools"), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(500, "tools"), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(400, "maximum context length is 8192 tokens"), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(400, ""), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(400, NULL), 0);
    /* in-stream error objects have no status */
    ASSERT_EQ(runcmd_reject_hint_applies(0, "overloaded"), 0);
    ASSERT_EQ(runcmd_reject_hint_applies(0, "tools"), 0);

    ASSERT_STR_EQ(runcmd_reject_hint(),
        "[If this provider does not accept Nutshell's tool calls, set Settings > "
        "AI Assistant > Provider > Commands as tool calls to Off]");
    TEST_END();
}
