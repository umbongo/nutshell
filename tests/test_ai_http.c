#include "test_framework.h"
#include "ai_http.h"
#include "ai_prompt.h"
#include <string.h>
#include <stdlib.h>

int test_ai_http_response_free_null(void) {
    TEST_BEGIN();
    ai_http_response_free(NULL); /* must not crash */
    AiHttpResponse resp;
    memset(&resp, 0, sizeof(resp));
    ai_http_response_free(&resp); /* NULL body is safe */
    ASSERT_NULL(resp.body);
    TEST_END();
}

int test_ai_http_response_free_allocated(void) {
    TEST_BEGIN();
    AiHttpResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.body = malloc(64);
    ASSERT_NOT_NULL(resp.body);
    memcpy(resp.body, "test", 5);
    resp.body_len = 4;
    ai_http_response_free(&resp);
    ASSERT_NULL(resp.body);
    ASSERT_EQ((int)resp.body_len, 0);
    TEST_END();
}

int test_ai_http_stub_conversation_flow(void) {
    TEST_BEGIN();
    /* Test the full flow: build request body, simulate response, parse it */
    AiConversation conv;
    ai_conv_init(&conv, "deepseek-chat");
    ai_conv_add(&conv, AI_ROLE_SYSTEM, "You are helpful.");
    ai_conv_add(&conv, AI_ROLE_USER, "List files");

    char body[4096];
    size_t n = ai_build_request_body(&conv, body, sizeof(body));
    ASSERT_TRUE(n > 0);

    /* Simulate a response from the API */
    const char *fake_response =
        "{\"choices\":[{\"message\":{\"content\":"
        "\"I'll list the files for you.\\n[EXEC]ls -la[/EXEC]\"}}]}";

    char content[1024];
    ASSERT_EQ(ai_parse_response(fake_response, content, sizeof(content)), 0);
    ASSERT_TRUE(strstr(content, "list the files") != NULL);

    char cmd[256];
    ASSERT_EQ(ai_extract_command(content, cmd, sizeof(cmd)), 1);
    ASSERT_STR_EQ(cmd, "ls -la");
    TEST_END();
}

int test_ai_http_stub_error_response(void) {
    TEST_BEGIN();
    /* Simulate a 401 error response */
    const char *error_json =
        "{\"error\":{\"message\":\"Invalid API key\",\"type\":\"auth_error\"}}";

    char content[256];
    /* No choices array, so parse should fail */
    ASSERT_EQ(ai_parse_response(error_json, content, sizeof(content)), -1);
    TEST_END();
}
