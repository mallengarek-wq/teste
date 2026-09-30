#ifndef RA_PPSSPP_MEMORY_H
#define RA_PPSSPP_MEMORY_H

#include <stdint.h>
struct rc_client_t;
uint32_t ra_psp_read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, struct rc_client_t* client);

#endif
