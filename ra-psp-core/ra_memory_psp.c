#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "ra_memory_psp.h"

#define RA_PSP_RAM_BASE 0x08000000u
#define RA_PSP_RAM_SIZE_32MB 0x02000000u
#define RA_PSP_RAM_SIZE_64MB 0x04000000u

static uint32_t g_ram_size = RA_PSP_RAM_SIZE_64MB;

int ra_psp_memory_range_valid(uint32_t address, uint32_t num_bytes) {
    uint64_t end = (uint64_t)address + (uint64_t)num_bytes;
    return num_bytes > 0 && end <= g_ram_size;
}

uint32_t ra_psp_read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, struct rc_client_t* client) {
    const void* src;
    (void)client;

    if (!buffer || !ra_psp_memory_range_valid(address, num_bytes))
        return 0;

    src = (const void*)(uintptr_t)(RA_PSP_RAM_BASE + address);
    memcpy(buffer, src, num_bytes);
    return num_bytes;
}
