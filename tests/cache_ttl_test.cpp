#include "cache_test_fixture.h"

#include <atomic>
#include <ctime>
#include <thread>
#include <unistd.h>
#include <vector>

TEST_F(CacheEngineTest, TtlValueCanBeReadBeforeExpiration) {
    EXPECT_TRUE(cache_put_ttl(shared_memory, "session:1", "active", 10));

    char value[MAX_VAL_LEN] = {};

    EXPECT_TRUE(cache_get(shared_memory, "session:1", value, nullptr));
    EXPECT_STREQ("active", value);
}

TEST_F(CacheEngineTest, ExpiredTtlValueIsRemovedOnGet) {
    EXPECT_TRUE(cache_put_ttl(shared_memory, "session:1", "temporary", 1));

    sleep(2);

    char value[MAX_VAL_LEN] = {};
    EXPECT_FALSE(cache_get(shared_memory, "session:1", value, nullptr));

    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    EXPECT_EQ(0u, header->entry_count);
}

TEST_F(CacheEngineTest, ExpirationBoundaryIsInclusive) {
    ASSERT_TRUE(cache_put_ttl(shared_memory, "boundary", "value", 10));
    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    int32_t slot = find_slot(shared_memory, "boundary");
    ASSERT_GE(slot, 0);
    pthread_rwlock_wrlock(&header->rwlock);
    CacheEntry* entry = get_entry_ptr(shared_memory, static_cast<uint32_t>(slot));
    entry->timestamp = static_cast<uint64_t>(std::time(nullptr)) - 10;
    pthread_rwlock_unlock(&header->rwlock);

    char value[MAX_VAL_LEN] = {};
    EXPECT_FALSE(cache_get(shared_memory, "boundary", value, nullptr));
    EXPECT_EQ(0u, header->entry_count);
}

TEST_F(CacheEngineTest, ConcurrentExpiredGetsCountEachMissAndRemoveOnce) {
    ASSERT_TRUE(cache_put_ttl(shared_memory, "expired", "value", 1));
    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    int32_t slot = find_slot(shared_memory, "expired");
    ASSERT_GE(slot, 0);
    pthread_rwlock_wrlock(&header->rwlock);
    get_entry_ptr(shared_memory, static_cast<uint32_t>(slot))->timestamp =
        static_cast<uint64_t>(std::time(nullptr)) - 1;
    header->total_hits = 0;
    header->total_misses = 0;
    header->total_collisions = 0;
    pthread_rwlock_unlock(&header->rwlock);

    const uint32_t thread_count = 8;
    const uint32_t queries_per_thread = 40;
    std::atomic<uint32_t> unexpected_hits(0);
    std::vector<std::thread> threads;
    for (uint32_t t = 0; t < thread_count; ++t) {
        threads.push_back(std::thread([this, queries_per_thread, &unexpected_hits]() {
            char value[MAX_VAL_LEN] = {};
            for (uint32_t i = 0; i < queries_per_thread; ++i) {
                if (cache_get(shared_memory, "expired", value, nullptr)) {
                    ++unexpected_hits;
                }
            }
        }));
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(0u, unexpected_hits.load());
    EXPECT_EQ(0u, header->total_hits);
    EXPECT_EQ(thread_count * queries_per_thread, header->total_misses);
    EXPECT_EQ(0u, header->entry_count);
}

TEST_F(CacheEngineTest, UpdateRejectsExpiredEntryWithoutHidingCollisions) {
    ASSERT_TRUE(cache_put_ttl(shared_memory, "a", "expired-value", 1));
    ASSERT_TRUE(cache_put(shared_memory, "i", "live-value"));

    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    pthread_rwlock_wrlock(&header->rwlock);
    for (uint32_t i = 0; i < header->capacity; ++i) {
        CacheEntry* entry = get_entry_ptr(shared_memory, i);
        if (entry->state == SlotState::OCCUPIED &&
            std::strcmp(entry->key, "a") == 0) {
            entry->timestamp = static_cast<uint64_t>(std::time(nullptr)) - 1;
            break;
        }
    }
    pthread_rwlock_unlock(&header->rwlock);

    EXPECT_FALSE(cache_update(shared_memory, "a", "resurrected"));
    EXPECT_EQ(1u, header->entry_count);
    char value[MAX_VAL_LEN] = {};
    EXPECT_FALSE(cache_get(shared_memory, "a", value, nullptr));
    EXPECT_EQ(1u, header->entry_count);
    EXPECT_TRUE(cache_get(shared_memory, "i", value, nullptr));
    EXPECT_STREQ("live-value", value);
}

TEST_F(CacheEngineTest, SuccessfulUpdateRefreshesTimestampAndRetainsTtl) {
    ASSERT_TRUE(cache_put_ttl(shared_memory, "refresh", "old-value", 10));
    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    int32_t slot = find_slot(shared_memory, "refresh");
    ASSERT_GE(slot, 0);
    pthread_rwlock_wrlock(&header->rwlock);
    CacheEntry* entry = get_entry_ptr(shared_memory, static_cast<uint32_t>(slot));
    entry->timestamp = static_cast<uint64_t>(std::time(nullptr)) - 9;
    pthread_rwlock_unlock(&header->rwlock);

    EXPECT_TRUE(cache_update(shared_memory, "refresh", "new-value"));
    pthread_rwlock_rdlock(&header->rwlock);
    entry = get_entry_ptr(shared_memory, static_cast<uint32_t>(slot));
    uint32_t ttl_seconds = entry->ttl_seconds;
    uint64_t timestamp = entry->timestamp;
    pthread_rwlock_unlock(&header->rwlock);
    EXPECT_EQ(10u, ttl_seconds);
    EXPECT_GE(timestamp, static_cast<uint64_t>(std::time(nullptr)) - 1);

    char value[MAX_VAL_LEN] = {};
    EXPECT_TRUE(cache_get(shared_memory, "refresh", value, nullptr));
    EXPECT_STREQ("new-value", value);
}
