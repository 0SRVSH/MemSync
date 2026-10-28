#include "cache_test_fixture.h"

#include <atomic>
#include <thread>
#include <vector>

TEST_F(CacheEngineTest, CollidingKeysCanBothBeRetrieved) {
    EXPECT_EQ(hash_key("a", capacity), hash_key("i", capacity));

    EXPECT_TRUE(cache_put(shared_memory, "a", "value-a"));
    EXPECT_TRUE(cache_put(shared_memory, "i", "value-i"));

    char value[MAX_VAL_LEN] = {};

    EXPECT_TRUE(cache_get(shared_memory, "a", value, nullptr));
    EXPECT_STREQ("value-a", value);

    EXPECT_TRUE(cache_get(shared_memory, "i", value, nullptr));
    EXPECT_STREQ("value-i", value);
}

TEST_F(CacheEngineTest, ReusesDeletedSlotWithoutHidingCollidingKey) {
    EXPECT_TRUE(cache_put(shared_memory, "a", "value-a"));
    EXPECT_TRUE(cache_put(shared_memory, "i", "value-i"));

    EXPECT_TRUE(cache_delete(shared_memory, "a"));
    EXPECT_TRUE(cache_put(shared_memory, "q", "value-q"));

    char value[MAX_VAL_LEN] = {};

    EXPECT_TRUE(cache_get(shared_memory, "i", value, nullptr));
    EXPECT_STREQ("value-i", value);

    EXPECT_TRUE(cache_get(shared_memory, "q", value, nullptr));
    EXPECT_STREQ("value-q", value);

    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    EXPECT_EQ(2u, header->entry_count);
}

TEST_F(CacheEngineTest, ConcurrentCollisionLookupsUpdateTelemetrySafely) {
    ASSERT_TRUE(cache_put(shared_memory, "a", "value-a"));
    ASSERT_TRUE(cache_put(shared_memory, "i", "value-i"));

    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    pthread_rwlock_wrlock(&header->rwlock);
    header->total_hits = 0;
    header->total_misses = 0;
    header->total_collisions = 0;
    pthread_rwlock_unlock(&header->rwlock);

    const uint32_t thread_count = 6;
    const uint32_t queries_per_thread = 50;
    std::atomic<uint32_t> lookup_failures(0);
    std::vector<std::thread> threads;
    for (uint32_t t = 0; t < thread_count; ++t) {
        threads.push_back(std::thread([this, queries_per_thread, &lookup_failures]() {
            char value[MAX_VAL_LEN] = {};
            for (uint32_t i = 0; i < queries_per_thread; ++i) {
                if (!cache_get(shared_memory, "i", value, nullptr) ||
                    std::strcmp(value, "value-i") != 0) {
                    ++lookup_failures;
                }
            }
        }));
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(0u, lookup_failures.load());
    EXPECT_EQ(thread_count * queries_per_thread, header->total_hits);
    EXPECT_EQ(0u, header->total_misses);
    EXPECT_EQ(thread_count * queries_per_thread, header->total_collisions);
}
