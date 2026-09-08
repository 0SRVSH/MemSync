#ifndef CACHE_ENGINE_H
#define CACHE_ENGINE_H

#include <cstdint>
#include <cstddef>

// 1. Fixed Configuration Limits
constexpr size_t MAX_KEY_SIZE = 64;
constexpr size_t MAX_VALUE_SIZE = 256;
constexpr size_t TOTAL_SLOTS = 1024;
constexpr uint32_t INVALID_OFFSET = 0xFFFFFFFF; // Replaces NULL pointer

// Slot status flags
enum SlotStatus : uint8_t {
    SLOT_EMPTY = 0,
    SLOT_OCCUPIED = 1,
    SLOT_DELETED = 2
};

// 2. Individual Data Record Layout
struct CacheEntry {
    uint8_t status;                   // SLOT_EMPTY, SLOT_OCCUPIED, or SLOT_DELETED
    char key[MAX_KEY_SIZE];           // Fixed-length string key
    char value[MAX_VALUE_SIZE];       // Fixed-length string value
    uint32_t next_offset;             // Byte offset to next slot (for collision chaining)
};

// 3. Global Memory Header (Placed at byte 0 of shared memory)
struct CacheHeader {
    uint32_t total_slots;             // Maximum slots allocated
    uint32_t used_slots;              // Currently occupied slots
    uint32_t data_offset;             // Byte offset where CacheEntry array starts
};

// 4. Function Prototypes
uint32_t hash_key(const char* key);
void init_cache_layout(void* shm_base);
bool cache_put(void* shm_base, const char* key, const char* value);
bool cache_get(void* shm_base, const char* key, char* out_value);

#endif // CACHE_ENGINE_H