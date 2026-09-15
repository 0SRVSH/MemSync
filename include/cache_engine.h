#ifndef CACHE_ENGINE_H
#define CACHE_ENGINE_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <ctime>

#define CACHE_MAGIC 0xDEADBEEF
#define MAX_KEY_LEN 64
#define MAX_VAL_LEN 256

// Slot state for linear probing tombstone handling
enum class SlotState : uint8_t {
    EMPTY = 0,
    OCCUPIED = 1,
    DELETED = 2
};

struct CacheHeader {
    uint32_t magic;          // Initialization verification flag
    uint32_t capacity;       // Maximum slot capacity
    uint32_t entry_count;    // Active allocated entries
    uint32_t data_offset;    // Byte offset where entry array begins
};

struct CacheEntry {
    SlotState state;         // Slot state: EMPTY, OCCUPIED, or DELETED
    char key[MAX_KEY_LEN];   // Key identifier
    char value[MAX_VAL_LEN]; // Value payload
    uint64_t timestamp;      // Unix timestamp of last update
};

// Function declarations
uint32_t hash_key(const char* key, uint32_t capacity);
CacheEntry* get_entry_ptr(void* shm_base, uint32_t index);
int32_t find_slot(void* shm_base, const char* key);

bool cache_put(void* shm_base, const char* key, const char* value);
bool cache_get(void* shm_base, const char* key, char* out_value, uint64_t* out_timestamp);
bool cache_update(void* shm_base, const char* key, const char* new_value);
bool cache_delete(void* shm_base, const char* key); // <--- NEW

#endif // CACHE_ENGINE_H