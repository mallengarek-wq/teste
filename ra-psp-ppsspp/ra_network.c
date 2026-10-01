#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ra_network.h"

/*
 * PPSSPP 1.20.x does not provide TLS in its sceHttp HLE implementation.
 * Direct https:// RetroAchievements requests therefore reach port 443 as
 * plaintext HTTP and fail with HTTP 400.
 *
 * This emulator build talks only to a loopback bridge on the Windows host:
 *   http://127.0.0.1:55355/ra
 * The bridge forwards the original request to RetroAchievements over real
 * host-side HTTPS and returns the untouched HTTP status/body to rcheevos.
 */

typedef uint64_t SceULong64;
typedef enum { PSP_HTTP_VERSION_1_0 = 0, PSP_HTTP_VERSION_1_1 = 1 } PspHttpHttpVersion;
typedef enum { PSP_HTTP_METHOD_GET = 0, PSP_HTTP_METHOD_POST = 1, PSP_HTTP_METHOD_HEAD = 2 } PspHttpMethod;
typedef enum { PSP_HTTP_HEADER_OVERWRITE = 0, PSP_HTTP_HEADER_ADD = 1 } PspHttpAddHeaderMode;

extern int sceHttpInit(unsigned int);
extern int sceHttpEnd(void);
extern int sceHttpCreateTemplate(char*, int, int);
extern int sceHttpDeleteTemplate(int);
extern int sceHttpCreateConnectionWithURL(int, const char*, int);
extern int sceHttpDeleteConnection(int);
extern int sceHttpCreateRequestWithURL(int, PspHttpMethod, char*, SceULong64);
extern int sceHttpDeleteRequest(int);
extern int sceHttpSendRequest(int, void*, unsigned int);
extern int sceHttpReadData(int, void*, unsigned int);
extern int sceHttpGetStatusCode(int, int*);
extern int sceHttpSetResolveTimeOut(int, unsigned int);
extern int sceHttpSetConnectTimeOut(int, unsigned int);
extern int sceHttpSetSendTimeOut(int, unsigned int);
extern int sceHttpSetRecvTimeOut(int, unsigned int);
extern int sceHttpAddExtraHeader(int, const char*, char*, PspHttpAddHeaderMode);

#define RA_BRIDGE_URL "http://127.0.0.1:55355/ra"
#define RA_HTTP_POOL_SIZE 16000
#define RA_HTTP_CHUNK 4096
#define RA_HTTP_MAX_RESPONSE (512 * 1024)

static int g_http_initialized;
static int g_last_error;

int ra_net_last_error(void) { return g_last_error; }

static int ensure_http(void) {
    int rc;
    if (g_http_initialized) return 0;
    rc = sceHttpInit(RA_HTTP_POOL_SIZE);
    if (rc < 0) {
        g_last_error = rc;
        return rc;
    }
    g_http_initialized = 1;
    g_last_error = 0;
    return 0;
}

void ra_net_shutdown(void) {
    if (g_http_initialized) {
        sceHttpEnd();
        g_http_initialized = 0;
    }
}

void RC_CCONV ra_net_server_call(const rc_api_request_t* request,
                                 rc_client_server_callback_t callback,
                                 void* callback_data,
                                 rc_client_t* client) {
    rc_api_server_response_t response;
    int tmpl = -1, conn = -1, req = -1;
    int status = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
    char* body = NULL;
    size_t used = 0, cap = 0;
    const char* post = NULL;
    unsigned int post_len = 0;
    const char* target_method;
    const char* target_content_type;
    (void)client;

    memset(&response, 0, sizeof(response));
    response.http_status_code = RC_API_SERVER_RESPONSE_CLIENT_ERROR;

    if (!request || !request->url || !callback) return;
    if (ensure_http() < 0) goto done;

    tmpl = sceHttpCreateTemplate("RA-PSP-Bridge/0.7", PSP_HTTP_VERSION_1_1, 0);
    if (tmpl < 0) { g_last_error = tmpl; goto done; }

    sceHttpSetResolveTimeOut(tmpl, 3000000);
    sceHttpSetConnectTimeOut(tmpl, 3000000);
    sceHttpSetSendTimeOut(tmpl, 5000000);
    sceHttpSetRecvTimeOut(tmpl, 15000000);

    conn = sceHttpCreateConnectionWithURL(tmpl, RA_BRIDGE_URL, 1);
    if (conn < 0) { g_last_error = conn; goto done; }

    post = request->post_data;
    post_len = post ? (unsigned int)strlen(post) : 0;

    /* Always POST to the local bridge. X-RA-Method preserves the original
       upstream method. */
    req = sceHttpCreateRequestWithURL(conn, PSP_HTTP_METHOD_POST,
        (char*)RA_BRIDGE_URL, (SceULong64)post_len);
    if (req < 0) { g_last_error = req; goto done; }

    target_method = post ? "POST" : "GET";
    target_content_type = (request->content_type && request->content_type[0])
        ? request->content_type : "application/x-www-form-urlencoded";

    sceHttpAddExtraHeader(req, "X-RA-Target", (char*)request->url, PSP_HTTP_HEADER_OVERWRITE);
    sceHttpAddExtraHeader(req, "X-RA-Method", (char*)target_method, PSP_HTTP_HEADER_OVERWRITE);
    sceHttpAddExtraHeader(req, "X-RA-Content-Type", (char*)target_content_type, PSP_HTTP_HEADER_OVERWRITE);
    sceHttpAddExtraHeader(req, "Content-Type", "application/octet-stream", PSP_HTTP_HEADER_OVERWRITE);
    sceHttpAddExtraHeader(req, "Accept", "application/json", PSP_HTTP_HEADER_OVERWRITE);

    status = sceHttpSendRequest(req, (void*)post, post_len);
    if (status < 0) {
        g_last_error = status;
        response.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
        goto done;
    }

    if (sceHttpGetStatusCode(req, &status) < 0)
        status = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
    response.http_status_code = status;

    cap = RA_HTTP_CHUNK;
    body = (char*)malloc(cap + 1);
    if (!body) { g_last_error = -1002; goto done; }

    while (used < RA_HTTP_MAX_RESPONSE) {
        int n;
        if (cap - used < RA_HTTP_CHUNK) {
            size_t new_cap = cap * 2;
            char* p;
            if (new_cap > RA_HTTP_MAX_RESPONSE) new_cap = RA_HTTP_MAX_RESPONSE;
            if (new_cap <= cap) break;
            p = (char*)realloc(body, new_cap + 1);
            if (!p) { g_last_error = -1003; break; }
            body = p;
            cap = new_cap;
        }

        n = sceHttpReadData(req, body + used, (unsigned int)(cap - used));
        if (n < 0) {
            g_last_error = n;
            response.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
            used = 0;
            break;
        }
        if (n == 0) break;
        used += (size_t)n;
    }

    body[used] = '\0';
    response.body = body;
    response.body_length = used;
    if (response.http_status_code >= 200 && response.http_status_code < 500)
        g_last_error = 0;

done:
    callback(&response, callback_data);
    if (req >= 0) sceHttpDeleteRequest(req);
    if (conn >= 0) sceHttpDeleteConnection(conn);
    if (tmpl >= 0) sceHttpDeleteTemplate(tmpl);
    free(body);
}
