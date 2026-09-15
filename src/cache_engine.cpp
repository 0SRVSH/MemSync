#include "cache_engine.h"
#include <cstring>
#include <ctime>

uint32_t hash_key(const char* key, uint32_t capacity) {
    uint32_t hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % capacity;
}

CacheEntry* get_entry_ptr(void* shm_base, uint32_t index) {
    if (!shm_base) return nullptr;
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    uint32_t offset = header->data_offset + (index * sizeof(CacheEntry));
    return reinterpret_cast<CacheEntry*>(static_cast<uint8_t*>(shm_base) + offset);
}

// Linear probing: finds existing matching slot OR first available slot (EMPTY or DELETED)
int32_t find_slot(void* shm_base, const char* key) {
    if (!shm_base || !key) return -1;
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    uint32_t start_index = hash_key(key, header->capacity);
    int32_t first_deleted_idx = -1;

    for (uint32_t i = 0; i < header->capacity; i++) {
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);

        if (entry->state == SlotState::EMPTY) {
            // Key does not exist; return first available tombstone slot or this empty slot
            return (first_deleted_idx != -1) ? first_deleted_idx : curr_index;
        }

        if (entry->state == SlotState::DELETED) {
            // Track first tombstone encountered for reuse during PUT
            if (first_deleted_idx == -1) {
                first_deleted_idx = curr_index;
            }
        } else if (entry->state == SlotState::OCCUPIED && std::strcmp(entry->key, key) == 0) {
            // Found existing key
            return curr_index;
        }
    }

    return first_deleted_idx; // Return reusable tombstone if table is otherwise full
}

bool cache_put(void* shm_base, const char* key, const char* value) {
    if (!shm_base || !key || !value) return false;

    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    int32_t slot_idx = find_slot(shm_base, key);

    if (slot_idx < 0) return false; // Cache is completely full

    CacheEntry* entry = get_entry_ptr(shm_base, slot_idx);

    if (entry->state != SlotState::OCCUPIED) {
        header->entry_count++;
    }

    entry->state = SlotState::OCCUPIED;
    std::strncpy(entry->key, key, MAX_KEY_LEN - 1);
    entry->key[MAX_KEY_LEN - 1] = '\0';

    std::strncpy(entry->value, value, MAX_VAL_LEN - 1);
    entry->value[MAX_VAL_LEN - 1] = '\0';

    entry->timestamp = static_cast<uint64_t>(std::time(nullptr));
    return true;
}

bool cache_get(void* shm_base, const char* key, char* out_value, uint64_t* out_timestamp) {
    if (!shm_base || !key || !out_value) return false;

    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    uint32_t start_index = hash_key(key, header->capacity);

    for (uint32_t i = 0; i < header->capacity; i++) {
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);

        if (entry->state == SlotState::EMPTY) {
            return false; // Key does not exist
        }
        
        if (entry->state == SlotState::OCCUPIED && std::strcmp(entry->key, key) == 0) {
            std::strncpy(out_value, entry->value, MAX_VAL_LEN - 1);
            out_value[MAX_VAL_LEN - 1] = '\0';
            if (out_timestamp) {
                *out_timestamp = entry->timestamp;
            }
            return true;
        }
    }
    return false;
}

bool cache_update(void* shm_base, const char* key, const char* new_value) {
    if (!shm_base || !key || !new_value) return false;

    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    uint32_t start_index = hash_key(key, header->capacity);

    for (uint32_t i = 0; i < header->capacity; i++) {
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);

        if (entry->state == SlotState::EMPTY) {
            return false; // Key not found
        }
        
        if (entry->state == SlotState::OCCUPIED && std::strcmp(entry->key, key) == 0) {
            std::strncpy(entry->value, new_value, MAX_VAL_LEN - 1);
            entry->value[MAX_VAL_LEN - 1] = '\0';
            entry->timestamp = static_cast<uint64_t>(std::time(nullptr));
            return true;
        }
    }
    return false;
}

// Mark record slot as DELETED (Tombstone)
bool cache_delete(void* shm_base, const char* key) {
    if (!shm_base || !key) return false;

    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    uint32_t start_index = hash_key(key, header->capacity);

    for (uint32_t i = 0; i < header->capacity; i++) {
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);

        if (entry->state == SlotState::EMPTY) {
            return false; // Key does not exist
        }
        
        if (entry->state == SlotState::OCCUPIED && std::strcmp(entry->key, key) == 0) {
            entry->state = SlotState::DELETED;
            entry->key[0] = '\0';
            entry->value[0] = '\0';
            entry->timestamp = 0;
            
            if (header->entry_count > 0) {
                header->entry_count--;
            }
            return true;
        }
    }
    return false;
}