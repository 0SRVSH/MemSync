#include "cache_engine.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char* const SHM_NAME = "/memsync_shm";
const size_t SHM_SIZE = 4 * 1024 * 1024;
const uint32_t CACHE_CAPACITY = 1000;

void print_json_string(const char* value) {
    std::cout << '"';
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
        switch (*p) {
        case '"': std::cout << "\\\""; break;
        case '\\': std::cout << "\\\\"; break;
        case '\b': std::cout << "\\b"; break;
        case '\f': std::cout << "\\f"; break;
        case '\n': std::cout << "\\n"; break;
        case '\r': std::cout << "\\r"; break;
        case '\t': std::cout << "\\t"; break;
        default:
            if (*p < 0x20) {
                const char hex[] = "0123456789abcdef";
                std::cout << "\\u00" << hex[*p >> 4] << hex[*p & 0x0f];
            } else {
                std::cout << static_cast<char>(*p);
            }
        }
    }
    std::cout << '"';
}

bool valid_key(const char* key) {
    if (!key || key[0] == '\0') {
        std::cerr << "Key must not be empty.\n";
        return false;
    }
    if (std::strlen(key) >= MAX_KEY_LEN) {
        std::cerr << "Key exceeds the cache limit of " << MAX_KEY_LEN - 1 << " bytes.\n";
        return false;
    }
    return true;
}

bool valid_value(const char* value) {
    if (!value) {
        std::cerr << "Value is required.\n";
        return false;
    }
    if (std::strlen(value) >= MAX_VAL_LEN) {
        std::cerr << "Value exceeds the cache limit of " << MAX_VAL_LEN - 1 << " bytes.\n";
        return false;
    }
    return true;
}

bool initialize_cache() {
    int fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd == -1 && errno == EEXIST) {
        fd = shm_open(SHM_NAME, O_RDWR, 0666);
        if (fd == -1) {
            perror("Could not open the existing shared-memory cache");
            return false;
        }

        struct stat info;
        if (fstat(fd, &info) == -1 || static_cast<size_t>(info.st_size) < SHM_SIZE) {
            std::cerr << "Existing shared-memory cache has an invalid size. "
                         "Remove it with memsync_cleanup, then initialize again.\n";
            close(fd);
            return false;
        }

        void* base = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (base == MAP_FAILED) {
            perror("Could not map the existing shared-memory cache");
            return false;
        }

        CacheHeader* header = static_cast<CacheHeader*>(base);
        bool valid = header->magic == CACHE_MAGIC &&
                     header->capacity > 0 &&
                     header->capacity <= (SHM_SIZE - sizeof(CacheHeader)) / sizeof(CacheEntry) &&
                     header->data_offset >= sizeof(CacheHeader) &&
                     static_cast<size_t>(header->data_offset) +
                         static_cast<size_t>(header->capacity) * sizeof(CacheEntry) <= SHM_SIZE;
        munmap(base, SHM_SIZE);
        if (!valid) {
            std::cerr << "Existing shared-memory cache is not initialized or has an invalid layout. "
                         "Remove it with memsync_cleanup, then initialize again.\n";
            return false;
        }
        std::cout << "{\"ok\":true,\"message\":\"Existing cache is ready.\"}\n";
        return true;
    }
    if (fd == -1) {
        perror("Could not create shared-memory cache");
        return false;
    }

    if (ftruncate(fd, SHM_SIZE) == -1) {
        perror("Could not size shared-memory cache");
        close(fd);
        shm_unlink(SHM_NAME);
        return false;
    }

    void* base = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (base == MAP_FAILED) {
        perror("Could not map shared-memory cache");
        shm_unlink(SHM_NAME);
        return false;
    }

    std::memset(base, 0, SHM_SIZE);
    CacheHeader* header = static_cast<CacheHeader*>(base);
    header->capacity = CACHE_CAPACITY;
    header->entry_count = 0;
    header->data_offset = sizeof(CacheHeader);
    header->total_hits = 0;
    header->total_misses = 0;
    header->total_collisions = 0;

    if (!cache_init_lock(header)) {
        std::cerr << "Could not initialize the shared cache lock.\n";
        munmap(base, SHM_SIZE);
        shm_unlink(SHM_NAME);
        return false;
    }
    header->magic = CACHE_MAGIC;
    munmap(base, SHM_SIZE);
    std::cout << "{\"ok\":true,\"message\":\"Cache initialized.\"}\n";
    return true;
}

bool open_cache(int& fd, void*& base) {
    fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd == -1) {
        perror("Could not open cache; initialize it from the UI first");
        return false;
    }

    struct stat info;
    if (fstat(fd, &info) == -1 || static_cast<size_t>(info.st_size) < SHM_SIZE) {
        std::cerr << "Shared-memory cache has an invalid size.\n";
        close(fd);
        fd = -1;
        return false;
    }

    base = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        perror("Could not map shared-memory cache");
        close(fd);
        fd = -1;
        base = nullptr;
        return false;
    }

    CacheHeader* header = static_cast<CacheHeader*>(base);
    if (header->magic != CACHE_MAGIC ||
        header->capacity == 0 ||
        header->capacity > (SHM_SIZE - sizeof(CacheHeader)) / sizeof(CacheEntry) ||
        header->data_offset < sizeof(CacheHeader) ||
        static_cast<size_t>(header->data_offset) +
            static_cast<size_t>(header->capacity) * sizeof(CacheEntry) > SHM_SIZE) {
        std::cerr << "Shared-memory cache is not initialized or has an invalid layout.\n";
        munmap(base, SHM_SIZE);
        close(fd);
        fd = -1;
        base = nullptr;
        return false;
    }
    return true;
}

void close_cache(int fd, void* base) {
    munmap(base, SHM_SIZE);
    close(fd);
}

void print_stats(const CacheHeader* header) {
    std::cout << "{\"capacity\":" << header->capacity
              << ",\"entries\":" << header->entry_count
              << ",\"hits\":" << header->total_hits
              << ",\"misses\":" << header->total_misses
              << ",\"collisions\":" << header->total_collisions << '}';
}

int run_command(int argc, char** argv) {
    const std::string command(argv[1]);
    if (command == "init") {
        return initialize_cache() ? 0 : 1;
    }

    int fd = -1;
    void* base = nullptr;
    if (!open_cache(fd, base)) {
        return 1;
    }

    CacheHeader* header = static_cast<CacheHeader*>(base);
    int result = 0;

    if (command == "stats" && argc == 2) {
        pthread_rwlock_rdlock(&header->rwlock);
        print_stats(header);
        pthread_rwlock_unlock(&header->rwlock);
        std::cout << '\n';
    } else if (command == "list" && argc == 2) {
        pthread_rwlock_rdlock(&header->rwlock);
        std::cout << "{\"items\":[";
        bool first = true;
        for (uint32_t i = 0; i < header->capacity; ++i) {
            CacheEntry* entry = get_entry_ptr(base, i);
            if (entry->state != SlotState::OCCUPIED) {
                continue;
            }
            uint64_t now = static_cast<uint64_t>(std::time(nullptr));
            bool expired = entry->ttl_seconds > 0 &&
                           now - entry->timestamp > entry->ttl_seconds;
            if (!first) {
                std::cout << ',';
            }
            first = false;
            std::cout << "{\"key\":";
            print_json_string(entry->key);
            std::cout << ",\"value\":";
            print_json_string(entry->value);
            std::cout << ",\"timestamp\":" << entry->timestamp
                      << ",\"ttl_seconds\":" << entry->ttl_seconds
                      << ",\"expired\":" << (expired ? "true" : "false") << '}';
        }
        std::cout << "],\"stats\":";
        print_stats(header);
        std::cout << "}\n";
        pthread_rwlock_unlock(&header->rwlock);
    } else if (command == "get" && argc == 3 && valid_key(argv[2])) {
        char value[MAX_VAL_LEN] = {};
        uint64_t timestamp = 0;
        if (cache_get(base, argv[2], value, &timestamp)) {
            std::cout << "{\"found\":true,\"value\":";
            print_json_string(value);
            std::cout << ",\"timestamp\":" << timestamp << "}\n";
        } else {
            std::cout << "{\"found\":false}\n";
        }
    } else if (command == "put" && (argc == 4 || argc == 5) &&
               valid_key(argv[2]) && valid_value(argv[3])) {
        uint32_t ttl = 0;
        if (argc == 5) {
            char* end = nullptr;
            unsigned long parsed = std::strtoul(argv[4], &end, 10);
            if (!end || *end != '\0' || parsed > UINT32_MAX) {
                std::cerr << "TTL must be an integer from 0 to " << UINT32_MAX << ".\n";
                result = 1;
            } else {
                ttl = static_cast<uint32_t>(parsed);
            }
        }
        if (result == 0) {
            bool ok = cache_put_ttl(base, argv[2], argv[3], ttl);
            if (ok) {
                std::cout << "{\"ok\":true}\n";
            } else {
                std::cerr << "Cache could not store the key.\n";
                result = 1;
            }
        }
    } else if (command == "update" && argc == 4 &&
               valid_key(argv[2]) && valid_value(argv[3])) {
        bool ok = cache_update(base, argv[2], argv[3]);
        if (ok) {
            std::cout << "{\"ok\":true}\n";
        } else {
            std::cerr << "Key not found or expired; update did not change the cache.\n";
            result = 1;
        }
    } else if (command == "delete" && argc == 3 && valid_key(argv[2])) {
        bool ok = cache_delete(base, argv[2]);
        if (ok) {
            std::cout << "{\"ok\":true}\n";
        } else {
            std::cerr << "Key not found; nothing was deleted.\n";
            result = 1;
        }
    } else {
        std::cerr << "Invalid command or arguments.\n";
        result = 2;
    }

    close_cache(fd, base);
    return result;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: memsync_ui_bridge init|stats|list|get|put|update|delete [arguments]\n";
        return 2;
    }
    return run_command(argc, argv);
}
