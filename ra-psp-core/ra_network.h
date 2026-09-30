#ifndef RA_NETWORK_H
#define RA_NETWORK_H

#include "rc_client.h"

void RC_CCONV ra_net_server_call(const rc_api_request_t* request,
                                 rc_client_server_callback_t callback,
                                 void* callback_data,
                                 rc_client_t* client);
void ra_net_shutdown(void);

#endif
