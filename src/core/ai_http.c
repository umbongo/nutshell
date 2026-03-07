#include "ai_http.h"
#include <stdlib.h>
#include <string.h>

void ai_http_response_free(AiHttpResponse *resp)
{
    if (!resp) return;
    free(resp->body);
    resp->body = NULL;
    resp->body_len = 0;
}
