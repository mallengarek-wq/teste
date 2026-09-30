#include <string.h>

#include "rc_client.h"
#include "ra_rc_bridge.h"
#include "ra_memory_psp.h"
#include "ra_network.h"

static rc_client_t* g_client;
static ra_ui_event_cb_t g_ui_cb;
static void* g_ui_userdata;

static void copy_text(char* dst, unsigned int dst_size, const char* src) {
    unsigned int i = 0;
    if (!dst || !dst_size) return;
    if (!src) { dst[0] = '\0'; return; }
    while (i + 1 < dst_size && src[i]) { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
}

static void emit_simple(ra_ui_event_type_t type) {
    ra_ui_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    if (g_ui_cb) g_ui_cb(&ev, g_ui_userdata);
}

static void RC_CCONV on_rc_event(const rc_client_event_t* event, rc_client_t* client) {
    ra_ui_event_t ev;
    (void)client;
    if (!event) return;

    memset(&ev, 0, sizeof(ev));

    switch (event->type) {
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
            if (!event->achievement) return;
            ev.type = RA_UI_EVENT_ACHIEVEMENT_UNLOCKED;
            ev.achievement_id = event->achievement->id;
            ev.points = event->achievement->points;
            copy_text(ev.title, sizeof(ev.title), event->achievement->title);
            copy_text(ev.description, sizeof(ev.description), event->achievement->description);
            copy_text(ev.badge_url, sizeof(ev.badge_url), event->achievement->badge_url);
            break;

        case RC_CLIENT_EVENT_DISCONNECTED:
            ev.type = RA_UI_EVENT_SERVER_OFFLINE;
            break;

        case RC_CLIENT_EVENT_RECONNECTED:
            ev.type = RA_UI_EVENT_SERVER_ONLINE;
            break;

        default:
            return;
    }

    if (g_ui_cb) g_ui_cb(&ev, g_ui_userdata);
}

static void RC_CCONV on_login(int result, const char* error_message,
                              rc_client_t* client, void* userdata) {
    (void)error_message; (void)client; (void)userdata;
    emit_simple(result == RC_OK ? RA_UI_EVENT_LOGIN_OK : RA_UI_EVENT_LOGIN_FAILED);
}

static void RC_CCONV on_game_loaded(int result, const char* error_message,
                                    rc_client_t* client, void* userdata) {
    (void)error_message; (void)client; (void)userdata;
    emit_simple(result == RC_OK ? RA_UI_EVENT_GAME_LOADED : RA_UI_EVENT_GAME_LOAD_FAILED);
}

int ra_rc_init(ra_ui_event_cb_t ui_cb, void* userdata) {
    if (g_client) return 0;

    g_ui_cb = ui_cb;
    g_ui_userdata = userdata;

    g_client = rc_client_create(ra_psp_read_memory, ra_net_server_call);
    if (!g_client) return -1;

    rc_client_set_userdata(g_client, userdata);
    rc_client_set_event_handler(g_client, on_rc_event);
    rc_client_set_hardcore_enabled(g_client, 0);
    rc_client_set_allow_background_memory_reads(g_client, 0);
    rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_WARN, 0);
    return 0;
}

void ra_rc_shutdown(void) {
    if (!g_client) return;
    rc_client_unload_game(g_client);
    rc_client_destroy(g_client);
    g_client = 0;
    g_ui_cb = 0;
    g_ui_userdata = 0;
}

void ra_rc_do_frame(void) {
    if (g_client) rc_client_do_frame(g_client);
}

int ra_rc_login_with_token(const char* username, const char* token) {
    if (!g_client || !username || !token) return -1;
    return rc_client_begin_login_with_token(g_client, username, token, on_login, 0) ? 0 : -2;
}

int ra_rc_load_game_hash(const char* hash) {
    if (!g_client || !hash || !hash[0]) return -1;
    return rc_client_begin_load_game(g_client, hash, on_game_loaded, 0) ? 0 : -2;
}

int ra_rc_is_ready(void) {
    return g_client != 0;
}

rc_client_t* ra_rc_client(void) {
    return g_client;
}
