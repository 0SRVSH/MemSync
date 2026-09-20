#include "cache_engine.h"
#include <cstring>
#include <ctime>
#include <iostream>
#include <iomanip>

bool cache_init_lock(CacheHeader* header) {
    if (!header) return false;
    pthread_rwlockattr_t attr;
    if (pthread_rwlockattr_init(&attr) != 0) return false;
    if (pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
        pthread_rwlockattr_destroy(&attr);
        return false;
    }
    int res = pthread_rwlock_init(&header->rwlock, &attr);
    pthread_rwlockattr_destroy(&attr);
    return res == 0;
}

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

// Internal helper: assumes caller holds lock
int32_t find_slot_internal(void* shm_base, const char* key) {
    if (!shm_base || !key) return -1;
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    uint32_t start_index = hash_key(key, header->capacity);
    int32_t first_deleted_idx = -1;

    for (uint32_t i = 0; i < header->capacity; i++) {
        if (i > 0) {
            header->total_collisions++;
        }
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);

        if (entry->state == SlotState::EMPTY) {
            return (first_deleted_idx != -1) ? first_deleted_idx : curr_index;
        }

        if (entry->state == SlotState::DELETED) {
            if (first_deleted_idx == -1) {
                first_deleted_idx = curr_index;
            }
        } else if (entry->state == SlotState::OCCUPIED && std::strcmp(entry->key, key) == 0) {
            return curr_index;
        }
    }

    return first_deleted_idx;
}

int32_t find_slot(void* shm_base, const char* key) {
    if (!shm_base) return -1;
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    pthread_rwlock_rdlock(&header->rwlock);
    int32_t idx = find_slot_internal(shm_base, key);
    pthread_rwlock_unlock(&header->rwlock);
    return idx;
}

bool cache_put(void* shm_base, const char* key, const char* value) {
    if (!shm_base || !key || !value) return false;
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);

    pthread_rwlock_wrlock(&header->rwlock);
    int32_t slot_idx = find_slot_internal(shm_base, key);
    if (slot_idx < 0) {
        pthread_rwlock_unlock(&header->rwlock);
        return false;
    }

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
    pthread_rwlock_unlock(&header->rwlock);
    return true;
}

bool cache_get(void* shm_base, const char* key, char* out_value, uint64_t* out_timestamp) {
    if (!shm_base || !key || !out_value) return false;
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);

    pthread_rwlock_rdlock(&header->rwlock);
    uint32_t start_index = hash_key(key, header->capacity);

    for (uint32_t i = 0; i < header->capacity; i++) {
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);

        if (entry->state == SlotState::EMPTY) {
            header->total_misses++;
            pthread_rwlock_unlock(&header->rwlock);
            return false;
        }
        
        if (entry->state == SlotState::OCCUPIED && std::strcmp(entry->key, key) == 0) {
            header->total_hits++;
            std::strncpy(out_value, entry->value, MAX_VAL_LEN - 1);
            out_value[MAX_VAL_LEN - 1] = '\0';
            if (out_timestamp) {
                *out_timestamp = entry->timestamp;
            }
            pthread_rwlock_unlock(&header->rwlock);
            return true;
        }
    }
    header->total_misses++;
    pthread_rwlock_unlock(&header->rwlock);
    return false;
}

bool cache_update(void* shm_base, const char* key, const char* new_value) {
    if (!shm_base || !key || !new_value) return false;
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);

    pthread_rwlock_wrlock(&header->rwlock);
    uint32_t start_index = hash_key(key, header->capacity);

    for (uint32_t i = 0; i < header->capacity; i++) {
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);

        if (entry->state == SlotState::EMPTY) {
            pthread_rwlock_unlock(&header->rwlock);
            return false;
        }
        
        if (entry->state == SlotState::OCCUPIED && std::strcmp(entry->key, key) == 0) {
            std::strncpy(entry->value, new_value, MAX_VAL_LEN - 1);
            entry->value[MAX_VAL_LEN - 1] = '\0';
            entry->timestamp = static_cast<uint64_t>(std::time(nullptr));
            pthread_rwlock_unlock(&header->rwlock);
            return true;
        }
    }
    pthread_rwlock_unlock(&header->rwlock);
    return false;
}

bool cache_delete(void* shm_base, const char* key) {
    if (!shm_base || !key) return false;
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);

    pthread_rwlock_wrlock(&header->rwlock);
    uint32_t start_index = hash_key(key, header->capacity);

    for (uint32_t i = 0; i < header->capacity; i++) {
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);

        if (entry->state == SlotState::EMPTY) {
            pthread_rwlock_unlock(&header->rwlock);
            return false;
        }
        
        if (entry->state == SlotState::OCCUPIED && std::strcmp(entry->key, key) == 0) {
            entry->state = SlotState::DELETED;
            entry->key[0] = '\0';
            entry->value[0] = '\0';
            entry->timestamp = 0;
            
            if (header->entry_count > 0) {
                header->entry_count--;
            }
            pthread_rwlock_unlock(&header->rwlock);
            return true;
        }
    }
    pthread_rwlock_unlock(&header->rwlock);
    return false;
}

void cache_print_telemetry(void* shm_base) {
    if (!shm_base) return;
    const CacheHeader* h = static_cast<const CacheHeader*>(shm_base);
    
    pthread_rwlock_rdlock(const_cast<pthread_rwlock_t*>(&h->rwlock));
    double load_factor = (h->capacity > 0) ? (static_cast<double>(h->entry_count) / h->capacity) * 100.0 : 0.0;
    uint64_t total_queries = h->total_hits + h->total_misses;
    double hit_ratio = (total_queries > 0) ? (static_cast<double>(h->total_hits) / total_queries) * 100.0 : 0.0;
    uint32_t cap = h->capacity;
    uint32_t count = h->entry_count;
    uint64_t hits = h->total_hits;
    uint64_t misses = h->total_misses;
    uint64_t collisions = h->total_collisions;
    pthread_rwlock_unlock(const_cast<pthread_rwlock_t*>(&h->rwlock));

    std::cout << "\n=== MemSync DBMS Telemetry Report ===" << std::endl;
    std::cout << "Capacity          : " << cap << std::endl;
    std::cout << "Active Entries    : " << count << std::endl;
    std::cout << "Load Factor (\u03b1)    : " << std::fixed << std::setprecision(2) << load_factor << "%" << std::endl;
    if (load_factor > 70.0) {
        std::cout << "[WARN] \u03b1 > 70%: Linear probing performance degradation threshold reached!" << std::endl;
    }
    std::cout << "Total Hits        : " << hits << std::endl;
    std::cout << "Total Misses      : " << misses << std::endl;
    std::cout << "Hit Ratio         : " << std::fixed << std::setprecision(2) << hit_ratio << "%" << std::endl;
    std::cout << "Total Collisions  : " << collisions << std::endl;
    std::cout << "=====================================\n" << std::endl;
}