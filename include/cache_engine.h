// In this file we have defined the structural layout of database in RAM.

// Created a "SlotState" system (EMPTY, OCCUPIED, DELETED) to handle record deletions cleanly.

// Set up fixed sizes for keys (64 bytes) and values (256 bytes).

// Declared all database operations (PUT, GET, UPDATE, DELETE).

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
// Instead of trying to remember random numbers—like whether 0 means empty, 1 means occupied, and 2 means deleted, an enum uses clear, readable words like EMPTY or OCCUPIED or DELETED
enum class SlotState : uint8_t {
    EMPTY = 0,
    OCCUPIED = 1,
    DELETED = 2
};

// We have used unsigned int as it guarantees the number can never be negative, and it is always the exact same size on any computer i.e. maintains Universal Consistency.

struct CacheHeader {
    uint32_t magic;          // Initialization of verification flag
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