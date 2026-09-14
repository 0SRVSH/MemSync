#include "../include/cache_engine.h"

// DJB2 String Hashing Function
uint32_t hash_key(const char* key, uint32_t capacity) {
    uint32_t hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash % capacity;
}

// Convert slot index to virtual address relative to shared memory base
CacheEntry* get_entry_ptr(void* shm_base, uint32_t index) {
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    uint32_t offset = header->data_offset + (index * sizeof(CacheEntry));
    return reinterpret_cast<CacheEntry*>(static_cast<uint8_t*>(shm_base) + offset);
}

// Linear probing search engine
int32_t find_slot(void* shm_base, const char* key) {
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    uint32_t start_index = hash_key(key, header->capacity);

    for (uint32_t i = 0; i < header->capacity; i++) {
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);

        // Return current slot if it's empty or matches the search key
        if (!entry->is_occupied || strcmp(entry->key, key) == 0) {
            return static_cast<int32_t>(curr_index);
        }
    }
    return -1; // Cache full
}

// Insert key-value pair and set timestamp
bool cache_put(void* shm_base, const char* key, const char* value) {
    int32_t slot_idx = find_slot(shm_base, key);
    if (slot_idx < 0) return false;

    CacheEntry* entry = get_entry_ptr(shm_base, slot_idx);
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);

    if (!entry->is_occupied) {
        header->entry_count++;
        entry->is_occupied = true;
    }

    strncpy(entry->key, key, MAX_KEY_LEN - 1);
    entry->key[MAX_KEY_LEN - 1] = '\0';

    strncpy(entry->value, value, MAX_VAL_LEN - 1);
    entry->value[MAX_VAL_LEN - 1] = '\0';

    entry->timestamp = static_cast<uint64_t>(time(nullptr));
    return true;
}

// Read value and timestamp matching key
bool cache_get(void* shm_base, const char* key, char* out_value, uint64_t* out_timestamp) {
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    uint32_t start_index = hash_key(key, header->capacity);

    for (uint32_t i = 0; i < header->capacity; i++) {
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);

        if (!entry->is_occupied) return false; // Key not found

        if (strcmp(entry->key, key) == 0) {
            strncpy(out_value, entry->value, MAX_VAL_LEN);
            if (out_timestamp) *out_timestamp = entry->timestamp;
            return true;
        }
    }
    return false;
}

// Overwrite existing record value and update timestamp
bool cache_update(void* shm_base, const char* key, const char* new_value) {
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    uint32_t start_index = hash_key(key, header->capacity);

    for (uint32_t i = 0; i < header->capacity; i++) {
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);

        if (!entry->is_occupied) return false;

        if (strcmp(entry->key, key) == 0) {
            strncpy(entry->value, new_value, MAX_VAL_LEN - 1);
            entry->value[MAX_VAL_LEN - 1] = '\0';
            entry->timestamp = static_cast<uint64_t>(time(nullptr));
            return true;
        }
    }
    return false;
}