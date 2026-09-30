#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pspkernel.h>
#include <pspsdk.h>
#include <psputility.h>
#include <pspnet_apctl.h>

#include "ra_network.h"

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

#define RA_HTTP_POOL_SIZE 24000
#define RA_HTTP_CHUNK 4096
#define RA_HTTP_MAX_RESPONSE (512 * 1024)

static int g_http_initialized;
static int g_inet_initialized;
static int g_ap_connected;
static int g_last_error;

int ra_net_last_error(void) { return g_last_error; }

static int ensure_network(void) {
    int state = 0;
    int i, rc;

    if (g_ap_connected) return 0;

    sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
    sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
    sceUtilityLoadNetModule(PSP_NET_MODULE_PARSEURI);
    sceUtilityLoadNetModule(PSP_NET_MODULE_PARSEHTTP);
    sceUtilityLoadNetModule(PSP_NET_MODULE_HTTP);
    sceUtilityLoadNetModule(PSP_NET_MODULE_SSL);

    if (!g_inet_initialized) {
        rc = pspSdkInetInit();
        if (rc < 0) { g_last_error = rc; return rc; }
        g_inet_initialized = 1;
    }

    rc = sceNetApctlGetState(&state);
    if (rc == 0 && state == 4) {
        g_ap_connected = 1;
        return 0;
    }

    /* PPSSPP emulates infrastructure profile 1 when WLAN/networking is enabled. */
    rc = sceNetApctlConnect(1);
    if (rc < 0) { g_last_error = rc; return rc; }

    for (i = 0; i < 200; ++i) {
        rc = sceNetApctlGetState(&state);
        if (rc < 0) { g_last_error = rc; return rc; }
        if (state == 4) {
            g_ap_connected = 1;
            return 0;
        }
        sceKernelDelayThread(50000);
    }

    g_last_error = -1001;
    return g_last_error;
}

static int ensure_http(void) {
    int rc;
    if (g_http_initialized) return 0;
    rc = ensure_network();
    if (rc < 0) return rc;
    rc = sceHttpInit(RA_HTTP_POOL_SIZE);
    if (rc < 0) { g_last_error = rc; return rc; }
    g_http_initialized = 1;
    return 0;
}

void ra_net_shutdown(void) {
    if (g_http_initialized) {
        sceHttpEnd();
        g_http_initialized = 0;
    }
    if (g_ap_connected) {
        sceNetApctlDisconnect();
        g_ap_connected = 0;
    }
    if (g_inet_initialized) {
        pspSdkInetTerm();
        g_inet_initialized = 0;
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
    (void)client;

    memset(&response, 0, sizeof(response));
    response.http_status_code = RC_API_SERVER_RESPONSE_CLIENT_ERROR;

    if (!request || !request->url || !callback) return;
    if (ensure_http() < 0) goto done;

    tmpl = sceHttpCreateTemplate("RA-PSP-PPSSPP/0.6 rcheevos", PSP_HTTP_VERSION_1_1, 0);
    if (tmpl < 0) { g_last_error = tmpl; goto done; }
    sceHttpSetResolveTimeOut(tmpl, 5000000);
    sceHttpSetConnectTimeOut(tmpl, 7000000);
    sceHttpSetSendTimeOut(tmpl, 7000000);
    sceHttpSetRecvTimeOut(tmpl, 10000000);

    conn = sceHttpCreateConnectionWithURL(tmpl, request->url, 0);
    if (conn < 0) { g_last_error = conn; goto done; }

    post = request->post_data;
    post_len = post ? (unsigned int)strlen(post) : 0;
    req = sceHttpCreateRequestWithURL(conn,
        post ? PSP_HTTP_METHOD_POST : PSP_HTTP_METHOD_GET,
        (char*)request->url, (SceULong64)post_len);
    if (req < 0) { g_last_error = req; goto done; }

    if (request->content_type && request->content_type[0])
        sceHttpAddExtraHeader(req, "Content-Type", (char*)request->content_type, PSP_HTTP_HEADER_OVERWRITE);
    sceHttpAddExtraHeader(req, "Accept", "application/json", PSP_HTTP_HEADER_OVERWRITE);

    status = sceHttpSendRequest(req, (void*)post, post_len);
    if (status < 0) {
        g_last_error = status;
        response.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
        goto done;
    }

    if (sceHttpGetStatusCode(req, &status) < 0) status = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
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
    if (response.http_status_code >= 200 && response.http_status_code < 400)
        g_last_error = 0;

done:
    callback(&response, callback_data);
    if (req >= 0) sceHttpDeleteRequest(req);
    if (conn >= 0) sceHttpDeleteConnection(conn);
    if (tmpl >= 0) sceHttpDeleteTemplate(tmpl);
    free(body);
}
