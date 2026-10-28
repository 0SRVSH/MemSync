#include "cache_engine.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

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
    CacheMapping mapping;
    if (!cache_open_shared(&mapping, true)) {
        return false;
    }
    bool created = mapping.created;
    cache_close_shared(&mapping);
    std::cout << "{\"ok\":true,\"message\":\""
              << (created ? "Cache initialized." : "Existing cache is ready.")
              << "\"}\n";
    return true;
}

bool open_cache(CacheMapping& mapping) {
    return cache_open_shared(&mapping, false);
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

    CacheMapping mapping;
    if (!open_cache(mapping)) {
        return 1;
    }

    void* base = mapping.base;
    CacheHeader* header = static_cast<CacheHeader*>(base);
    int result = 0;

    if (command == "stats" && argc == 2) {
        if (pthread_rwlock_rdlock(&header->rwlock) != 0) {
            std::cerr << "Could not acquire cache read lock for stats.\n";
            result = 1;
        } else {
            print_stats(header);
            pthread_rwlock_unlock(&header->rwlock);
            std::cout << '\n';
        }
    } else if (command == "list" && argc == 2) {
        if (pthread_rwlock_rdlock(&header->rwlock) != 0) {
            std::cerr << "Could not acquire cache read lock for listing.\n";
            result = 1;
        } else {
            std::cout << "{\"items\":[";
            bool first = true;
            for (uint32_t i = 0; i < header->capacity; ++i) {
                CacheEntry* entry = get_entry_ptr(base, i);
                if (entry->state != SlotState::OCCUPIED) {
                    continue;
                }
                uint64_t now = static_cast<uint64_t>(std::time(nullptr));
                bool expired = cache_entry_expired(*entry, now);
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
        }
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

    cache_close_shared(&mapping);
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
