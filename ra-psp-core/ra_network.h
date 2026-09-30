#ifndef RA_NETWORK_H
#define RA_NETWORK_H

#include "rc_client.h"

/* Transport hook required by rc_client. PSP HTTPS implementation comes next. */
void RC_CCONV ra_net_server_call(const rc_api_request_t* request,
                                 rc_client_server_callback_t callback,
                                 void* callback_data,
                                 rc_client_t* client);

#endif
