#include "ra_network.h"

void RC_CCONV ra_net_server_call(const rc_api_request_t* request,
                                 rc_client_server_callback_t callback,
                                 void* callback_data,
                                 rc_client_t* client) {
    /* HTTPS transport intentionally not faked. This is the next hardware milestone. */
    (void)request;
    (void)callback;
    (void)callback_data;
    (void)client;
}
