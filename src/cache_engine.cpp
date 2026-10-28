#include "cache_engine.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char* const SHM_NAME = "/memsync_shm";

bool valid_capacity(size_t size, uint32_t capacity) {
    return size >= sizeof(CacheHeader) &&
           capacity > 0 &&
           capacity <= (size - sizeof(CacheHeader)) / sizeof(CacheEntry);
}

bool lock_for_read(CacheHeader* header) {
    if (pthread_rwlock_rdlock(&header->rwlock) == 0) {
        return true;
    }
    std::cerr << "Could not acquire cache read lock.\n";
    return false;
}

bool lock_for_write(CacheHeader* header) {
    if (pthread_rwlock_wrlock(&header->rwlock) == 0) {
        return true;
    }
    std::cerr << "Could not acquire cache write lock.\n";
    return false;
}

void unlock_cache(CacheHeader* header) {
    if (pthread_rwlock_unlock(&header->rwlock) != 0) {
        std::cerr << "Could not release cache lock.\n";
    }
}

void remove_entry(CacheHeader* header, CacheEntry* entry) {
    entry->state = SlotState::DELETED;
    entry->key[0] = '\0';
    entry->value[0] = '\0';
    entry->timestamp = 0;
    entry->ttl_seconds = 0;
    if (header->entry_count > 0) {
        --header->entry_count;
    }
}

int32_t find_slot_internal(
    void* shm_base,
    const char* key,
    uint32_t* collision_count
) {
    if (!shm_base || !key) {
        return -1;
    }

    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    uint32_t start_index = hash_key(key, header->capacity);
    int32_t first_deleted_idx = -1;

    for (uint32_t i = 0; i < header->capacity; ++i) {
        if (i > 0 && collision_count) {
            ++*collision_count;
        }
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);

        if (entry->state == SlotState::EMPTY) {
            return (first_deleted_idx != -1) ? first_deleted_idx : curr_index;
        }
        if (entry->state == SlotState::DELETED) {
            if (first_deleted_idx == -1) {
                first_deleted_idx = static_cast<int32_t>(curr_index);
            }
        } else if (entry->state == SlotState::OCCUPIED &&
                   std::strcmp(entry->key, key) == 0) {
            return static_cast<int32_t>(curr_index);
        }
    }

    return first_deleted_idx;
}

bool evict_oldest_write(CacheHeader* header, void* shm_base) {
    int32_t oldest_idx = -1;
    uint64_t oldest_ts = UINT64_MAX;

    for (uint32_t i = 0; i < header->capacity; ++i) {
        CacheEntry* entry = get_entry_ptr(shm_base, i);
        if (entry->state == SlotState::OCCUPIED &&
            (oldest_idx == -1 || entry->timestamp < oldest_ts)) {
            oldest_ts = entry->timestamp;
            oldest_idx = static_cast<int32_t>(i);
        }
    }
    if (oldest_idx == -1) {
        return false;
    }

    remove_entry(header, get_entry_ptr(shm_base, static_cast<uint32_t>(oldest_idx)));
    return true;
}

void reclaim_expired(CacheHeader* header, void* shm_base, uint64_t now) {
    for (uint32_t i = 0; i < header->capacity; ++i) {
        CacheEntry* entry = get_entry_ptr(shm_base, i);
        if (entry->state == SlotState::OCCUPIED &&
            cache_entry_expired(*entry, now)) {
            remove_entry(header, entry);
        }
    }
}

int32_t find_key_locked(void* shm_base, const char* key) {
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    uint32_t start_index = hash_key(key, header->capacity);
    for (uint32_t i = 0; i < header->capacity; ++i) {
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);
        if (entry->state == SlotState::EMPTY) {
            return -1;
        }
        if (entry->state == SlotState::OCCUPIED &&
            std::strcmp(entry->key, key) == 0) {
            return static_cast<int32_t>(curr_index);
        }
    }
    return -1;
}

bool add_get_metrics(
    CacheHeader* header,
    bool found,
    uint32_t collisions
) {
    if (!lock_for_write(header)) {
        return false;
    }
    if (found) {
        ++header->total_hits;
    } else {
        ++header->total_misses;
    }
    header->total_collisions += collisions;
    unlock_cache(header);
    return true;
}

bool validate_ready_header(const void* shm_base, size_t size) {
    if (!cache_validate_layout(shm_base, size)) {
        return false;
    }
    CacheHeader* header = const_cast<CacheHeader*>(
        static_cast<const CacheHeader*>(shm_base)
    );
    if (!lock_for_read(header)) {
        return false;
    }
    bool valid = header->entry_count <= header->capacity;
    unlock_cache(header);
    return valid;
}

} // namespace

bool cache_init_lock(CacheHeader* header) {
    if (!header) {
        return false;
    }
    pthread_rwlockattr_t attr;
    if (pthread_rwlockattr_init(&attr) != 0) {
        return false;
    }
    if (pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
        pthread_rwlockattr_destroy(&attr);
        return false;
    }
    int result = pthread_rwlock_init(&header->rwlock, &attr);
    pthread_rwlockattr_destroy(&attr);
    return result == 0;
}

bool cache_initialize_region(void* shm_base, size_t size, uint32_t capacity) {
    if (!shm_base || !valid_capacity(size, capacity)) {
        return false;
    }

    std::memset(shm_base, 0, size);
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    header->version = CACHE_VERSION;
    header->capacity = capacity;
    header->entry_count = 0;
    header->data_offset = sizeof(CacheHeader);
    header->total_hits = 0;
    header->total_misses = 0;
    header->total_collisions = 0;

    if (!cache_init_lock(header)) {
        return false;
    }

    __atomic_store_n(&header->magic, CACHE_MAGIC, __ATOMIC_RELEASE);
    return true;
}

bool cache_validate_layout(const void* shm_base, size_t size) {
    if (!shm_base || size < sizeof(CacheHeader)) {
        return false;
    }
    const CacheHeader* header = static_cast<const CacheHeader*>(shm_base);
    if (__atomic_load_n(&header->magic, __ATOMIC_ACQUIRE) != CACHE_MAGIC) {
        return false;
    }
    return header->version == CACHE_VERSION &&
           valid_capacity(size, header->capacity) &&
           header->data_offset == sizeof(CacheHeader);
}

bool cache_entry_expired(const CacheEntry& entry, uint64_t now) {
    return entry.ttl_seconds > 0 &&
           now >= entry.timestamp &&
           now - entry.timestamp >= entry.ttl_seconds;
}

const char* cache_shared_memory_name() {
    return SHM_NAME;
}

bool cache_open_shared(
    CacheMapping* mapping,
    bool create_if_missing,
    const char* shared_memory_name
) {
    if (!mapping) {
        return false;
    }
    const char* name = shared_memory_name ? shared_memory_name : SHM_NAME;
    mapping->fd = -1;
    mapping->base = nullptr;
    mapping->size = CACHE_SHARED_MEMORY_SIZE;
    mapping->created = false;

    int fd = -1;
    bool created = false;
    if (create_if_missing) {
        fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd != -1) {
            created = true;
        } else if (errno == EEXIST) {
            fd = shm_open(name, O_RDWR, 0666);
        }
    } else {
        fd = shm_open(name, O_RDWR, 0666);
    }
    if (fd == -1) {
        perror("Could not open shared-memory cache");
        return false;
    }

    if (created) {
        if (ftruncate(fd, static_cast<off_t>(CACHE_SHARED_MEMORY_SIZE)) == -1) {
            perror("Could not size newly created shared-memory cache");
            close(fd);
            shm_unlink(name);
            return false;
        }
    } else {
        struct stat info;
        if (fstat(fd, &info) == -1) {
            perror("Could not inspect shared-memory cache");
            close(fd);
            return false;
        }
        if (info.st_size < static_cast<off_t>(CACHE_SHARED_MEMORY_SIZE)) {
            std::cerr << "Shared-memory cache is not ready or has an invalid size; "
                         "retry after initialization completes.\n";
            close(fd);
            return false;
        }
    }

    void* base = mmap(
        nullptr,
        CACHE_SHARED_MEMORY_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );
    if (base == MAP_FAILED) {
        perror("Could not map shared-memory cache");
        close(fd);
        if (created) {
            shm_unlink(name);
        }
        return false;
    }

    if (created) {
        if (!cache_initialize_region(
                base,
                CACHE_SHARED_MEMORY_SIZE,
                CACHE_DEFAULT_CAPACITY)) {
            std::cerr << "Could not initialize shared-memory cache.\n";
            munmap(base, CACHE_SHARED_MEMORY_SIZE);
            close(fd);
            shm_unlink(name);
            return false;
        }
    } else if (!validate_ready_header(base, CACHE_SHARED_MEMORY_SIZE)) {
        std::cerr << "Shared-memory cache is initializing or has an invalid "
                     "header/layout; retry after initialization completes.\n";
        munmap(base, CACHE_SHARED_MEMORY_SIZE);
        close(fd);
        return false;
    }

    mapping->fd = fd;
    mapping->base = base;
    mapping->created = created;
    return true;
}

void cache_close_shared(CacheMapping* mapping) {
    if (!mapping) {
        return;
    }
    if (mapping->base && mapping->base != MAP_FAILED) {
        munmap(mapping->base, mapping->size);
    }
    if (mapping->fd != -1) {
        close(mapping->fd);
    }
    mapping->fd = -1;
    mapping->base = nullptr;
    mapping->size = 0;
    mapping->created = false;
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
    if (!shm_base) {
        return nullptr;
    }
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    uint32_t offset = header->data_offset + (index * sizeof(CacheEntry));
    return reinterpret_cast<CacheEntry*>(
        static_cast<uint8_t*>(shm_base) + offset
    );
}

int32_t find_slot(void* shm_base, const char* key) {
    if (!shm_base || !key) {
        return -1;
    }
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);
    if (!lock_for_read(header)) {
        return -1;
    }
    int32_t index = find_slot_internal(shm_base, key, nullptr);
    unlock_cache(header);
    return index;
}

bool cache_put_ttl(
    void* shm_base,
    const char* key,
    const char* value,
    uint32_t ttl_sec
) {
    if (!shm_base || !key || !value) {
        return false;
    }
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);

    if (!lock_for_write(header)) {
        return false;
    }
    uint32_t collisions = 0;
    int32_t slot_idx = find_slot_internal(shm_base, key, &collisions);
    header->total_collisions += collisions;

    if (slot_idx < 0) {
        reclaim_expired(
            header,
            shm_base,
            static_cast<uint64_t>(std::time(nullptr))
        );
        collisions = 0;
        slot_idx = find_slot_internal(shm_base, key, &collisions);
        header->total_collisions += collisions;
    }
    if (slot_idx < 0) {
        if (!evict_oldest_write(header, shm_base)) {
            unlock_cache(header);
            return false;
        }
        collisions = 0;
        slot_idx = find_slot_internal(shm_base, key, &collisions);
        header->total_collisions += collisions;
        if (slot_idx < 0) {
            unlock_cache(header);
            return false;
        }
    }

    CacheEntry* entry = get_entry_ptr(shm_base, static_cast<uint32_t>(slot_idx));
    if (entry->state != SlotState::OCCUPIED) {
        ++header->entry_count;
    }

    entry->state = SlotState::OCCUPIED;
    std::strncpy(entry->key, key, MAX_KEY_LEN - 1);
    entry->key[MAX_KEY_LEN - 1] = '\0';
    std::strncpy(entry->value, value, MAX_VAL_LEN - 1);
    entry->value[MAX_VAL_LEN - 1] = '\0';
    entry->timestamp = static_cast<uint64_t>(std::time(nullptr));
    entry->ttl_seconds = ttl_sec;

    unlock_cache(header);
    return true;
}

bool cache_put(void* shm_base, const char* key, const char* value) {
    return cache_put_ttl(shm_base, key, value, 0);
}

bool cache_get(
    void* shm_base,
    const char* key,
    char* out_value,
    uint64_t* out_timestamp
) {
    if (!shm_base || !key || !out_value) {
        return false;
    }
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);

    if (!lock_for_read(header)) {
        return false;
    }
    uint32_t start_index = hash_key(key, header->capacity);
    uint32_t collisions = 0;
    bool expired = false;

    for (uint32_t i = 0; i < header->capacity; ++i) {
        if (i > 0) {
            ++collisions;
        }
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);

        if (entry->state == SlotState::EMPTY) {
            unlock_cache(header);
            add_get_metrics(header, false, collisions);
            return false;
        }

        if (entry->state == SlotState::OCCUPIED &&
            std::strcmp(entry->key, key) == 0) {
            uint64_t now = static_cast<uint64_t>(std::time(nullptr));
            if (cache_entry_expired(*entry, now)) {
                expired = true;
                break;
            }

            char value_copy[MAX_VAL_LEN];
            std::strncpy(value_copy, entry->value, MAX_VAL_LEN - 1);
            value_copy[MAX_VAL_LEN - 1] = '\0';
            uint64_t timestamp_copy = entry->timestamp;
            unlock_cache(header);

            std::strncpy(out_value, value_copy, MAX_VAL_LEN - 1);
            out_value[MAX_VAL_LEN - 1] = '\0';
            if (out_timestamp) {
                *out_timestamp = timestamp_copy;
            }
            add_get_metrics(header, true, collisions);
            return true;
        }
    }

    unlock_cache(header);
    if (expired) {
        if (!lock_for_write(header)) {
            return false;
        }
        int32_t current_idx = find_key_locked(shm_base, key);
        if (current_idx >= 0) {
            CacheEntry* current = get_entry_ptr(
                shm_base,
                static_cast<uint32_t>(current_idx)
            );
            if (cache_entry_expired(
                    *current,
                    static_cast<uint64_t>(std::time(nullptr)))) {
                remove_entry(header, current);
            }
        }
        ++header->total_misses;
        header->total_collisions += collisions;
        unlock_cache(header);
        return false;
    }

    add_get_metrics(header, false, collisions);
    return false;
}

bool cache_update(void* shm_base, const char* key, const char* new_value) {
    if (!shm_base || !key || !new_value) {
        return false;
    }
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);

    if (!lock_for_write(header)) {
        return false;
    }
    uint32_t start_index = hash_key(key, header->capacity);
    for (uint32_t i = 0; i < header->capacity; ++i) {
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);
        if (entry->state == SlotState::EMPTY) {
            header->total_collisions += i;
            unlock_cache(header);
            return false;
        }
        if (entry->state == SlotState::OCCUPIED &&
            std::strcmp(entry->key, key) == 0) {
            header->total_collisions += i;
            if (cache_entry_expired(
                    *entry,
                    static_cast<uint64_t>(std::time(nullptr)))) {
                remove_entry(header, entry);
                unlock_cache(header);
                return false;
            }
            std::strncpy(entry->value, new_value, MAX_VAL_LEN - 1);
            entry->value[MAX_VAL_LEN - 1] = '\0';
            entry->timestamp = static_cast<uint64_t>(std::time(nullptr));
            unlock_cache(header);
            return true;
        }
    }
    header->total_collisions += header->capacity - 1;
    unlock_cache(header);
    return false;
}

bool cache_delete(void* shm_base, const char* key) {
    if (!shm_base || !key) {
        return false;
    }
    CacheHeader* header = static_cast<CacheHeader*>(shm_base);

    if (!lock_for_write(header)) {
        return false;
    }
    uint32_t start_index = hash_key(key, header->capacity);
    for (uint32_t i = 0; i < header->capacity; ++i) {
        uint32_t curr_index = (start_index + i) % header->capacity;
        CacheEntry* entry = get_entry_ptr(shm_base, curr_index);
        if (entry->state == SlotState::EMPTY) {
            header->total_collisions += i;
            unlock_cache(header);
            return false;
        }
        if (entry->state == SlotState::OCCUPIED &&
            std::strcmp(entry->key, key) == 0) {
            header->total_collisions += i;
            remove_entry(header, entry);
            unlock_cache(header);
            return true;
        }
    }
    header->total_collisions += header->capacity - 1;
    unlock_cache(header);
    return false;
}

void cache_print_telemetry(void* shm_base) {
    if (!shm_base) {
        return;
    }
    const CacheHeader* header = static_cast<const CacheHeader*>(shm_base);

    if (!lock_for_read(const_cast<CacheHeader*>(header))) {
        return;
    }
    double load_factor = (header->capacity > 0)
        ? (static_cast<double>(header->entry_count) / header->capacity) * 100.0
        : 0.0;
    uint64_t total_queries = header->total_hits + header->total_misses;
    double hit_ratio = (total_queries > 0)
        ? (static_cast<double>(header->total_hits) / total_queries) * 100.0
        : 0.0;
    uint32_t capacity = header->capacity;
    uint32_t count = header->entry_count;
    uint64_t hits = header->total_hits;
    uint64_t misses = header->total_misses;
    uint64_t collisions = header->total_collisions;
    unlock_cache(const_cast<CacheHeader*>(header));

    std::cout << "\n=== MemSync DBMS Telemetry Report ===" << std::endl;
    std::cout << "Capacity          : " << capacity << std::endl;
    std::cout << "Active Entries    : " << count << std::endl;
    std::cout << "Load Factor (\u03b1)    : " << std::fixed << std::setprecision(2)
              << load_factor << "%" << std::endl;
    if (load_factor > 70.0) {
        std::cout << "[WARN] \u03b1 > 70%: Linear probing performance degradation threshold reached!"
                  << std::endl;
    }
    std::cout << "Total Hits        : " << hits << std::endl;
    std::cout << "Total Misses      : " << misses << std::endl;
    std::cout << "Hit Ratio         : " << std::fixed << std::setprecision(2)
              << hit_ratio << "%" << std::endl;
    std::cout << "Total Collisions  : " << collisions << std::endl;
    std::cout << "=====================================\n" << std::endl;
}
