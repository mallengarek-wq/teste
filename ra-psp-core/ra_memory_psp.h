#ifndef RA_MEMORY_PSP_H
#define RA_MEMORY_PSP_H

#include <stdint.h>

struct rc_client_t;

uint32_t ra_psp_read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, struct rc_client_t* client);
int ra_psp_memory_range_valid(uint32_t address, uint32_t num_bytes);

#endif
