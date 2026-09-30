#ifndef RA_RC_BRIDGE_H
#define RA_RC_BRIDGE_H

#include <stdint.h>

struct rc_client_t;
typedef struct rc_client_t rc_client_t;

typedef enum {
    RA_UI_EVENT_NONE = 0,
    RA_UI_EVENT_ACHIEVEMENT_UNLOCKED,
    RA_UI_EVENT_GAME_LOADED,
    RA_UI_EVENT_GAME_LOAD_FAILED,
    RA_UI_EVENT_LOGIN_OK,
    RA_UI_EVENT_LOGIN_FAILED,
    RA_UI_EVENT_SERVER_OFFLINE,
    RA_UI_EVENT_SERVER_ONLINE
} ra_ui_event_type_t;

typedef struct {
    ra_ui_event_type_t type;
    uint32_t achievement_id;
    uint32_t points;
    char title[96];
    char description[192];
    char badge_url[256];
} ra_ui_event_t;

typedef void (*ra_ui_event_cb_t)(const ra_ui_event_t* event, void* userdata);

int ra_rc_init(ra_ui_event_cb_t ui_cb, void* userdata);
void ra_rc_shutdown(void);
void ra_rc_do_frame(void);
int ra_rc_login_with_token(const char* username, const char* token);
int ra_rc_login_with_password(const char* username, const char* password);
int ra_rc_load_game_hash(const char* hash);
int ra_rc_is_ready(void);
rc_client_t* ra_rc_client(void);

#endif
