#include "cache_test_fixture.h"

#include <ctime>
#include <cstring>

namespace {

void set_timestamp(void* memory, const char* key, uint64_t timestamp) {
    CacheHeader* header = static_cast<CacheHeader*>(memory);
    for (uint32_t i = 0; i < header->capacity; ++i) {
        CacheEntry* entry = get_entry_ptr(memory, i);
        if (entry->state == SlotState::OCCUPIED &&
            std::strcmp(entry->key, key) == 0) {
            entry->timestamp = timestamp;
            return;
        }
    }
}

bool can_get(void* memory, const char* key) {
    char value[MAX_VAL_LEN] = {};
    return cache_get(memory, key, value, nullptr);
}

} // namespace

TEST_F(CacheEngineTest, InsertingPastCapacityEvictsOldestWriteEntry) {
    const char* keys[] = {
        "key-0", "key-1", "key-2", "key-3",
        "key-4", "key-5", "key-6", "key-7"
    };

    for (uint32_t i = 0; i < capacity; ++i) {
        ASSERT_TRUE(cache_put(shared_memory, keys[i], "old-value"));
        set_timestamp(shared_memory, keys[i], 100 + i);
    }

    ASSERT_TRUE(cache_put(shared_memory, "new-key", "new-value"));

    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    EXPECT_EQ(capacity, header->entry_count);
    EXPECT_FALSE(can_get(shared_memory, "key-0"));
    EXPECT_TRUE(can_get(shared_memory, "key-1"));
    EXPECT_TRUE(can_get(shared_memory, "new-key"));
}

TEST_F(CacheEngineTest, ExpiredEntryIsReclaimedBeforeEvictingLiveEntry) {
    const char* keys[] = {
        "key-0", "key-1", "key-2", "key-3",
        "key-4", "key-5", "key-6", "key-7"
    };

    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    for (uint32_t i = 0; i < capacity; ++i) {
        ASSERT_TRUE(cache_put_ttl(shared_memory, keys[i], "old-value", 10));
        set_timestamp(shared_memory, keys[i], now);
    }
    set_timestamp(shared_memory, "key-0", now - 10);
    set_timestamp(shared_memory, "key-1", now - 9);

    ASSERT_TRUE(cache_put(shared_memory, "new-key", "new-value"));

    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    EXPECT_EQ(capacity, header->entry_count);
    EXPECT_FALSE(can_get(shared_memory, "key-0"));
    EXPECT_TRUE(can_get(shared_memory, "key-1"));
    EXPECT_TRUE(can_get(shared_memory, "new-key"));
}

TEST_F(CacheEngineTest, EqualTimestampsEvictTheLowestOccupiedSlot) {
    const char* keys[] = {
        "key-0", "key-1", "key-2", "key-3",
        "key-4", "key-5", "key-6", "key-7"
    };
    for (uint32_t i = 0; i < capacity; ++i) {
        ASSERT_TRUE(cache_put(shared_memory, keys[i], "old-value"));
    }

    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    pthread_rwlock_wrlock(&header->rwlock);
    uint32_t first_occupied = capacity;
    char expected_eviction[MAX_KEY_LEN] = {};
    for (uint32_t i = 0; i < capacity; ++i) {
        CacheEntry* entry = get_entry_ptr(shared_memory, i);
        if (entry->state == SlotState::OCCUPIED) {
            entry->timestamp = 100;
            if (first_occupied == capacity) {
                first_occupied = i;
                std::strncpy(expected_eviction, entry->key, MAX_KEY_LEN - 1);
            }
        }
    }
    pthread_rwlock_unlock(&header->rwlock);

    ASSERT_LT(first_occupied, capacity);
    ASSERT_TRUE(cache_put(shared_memory, "new-key", "new-value"));
    EXPECT_FALSE(can_get(shared_memory, expected_eviction));
}
