#include "cache_test_fixture.h"

#include <atomic>
#include <cstring>
#include <sys/mman.h>
#include <thread>
#include <vector>

TEST_F(CacheEngineTest, PutThenGetReturnsStoredValue) {
    EXPECT_TRUE(cache_put(shared_memory, "user:1", "Alice"));

    char value[MAX_VAL_LEN] = {};
    uint64_t timestamp = 0;

    EXPECT_TRUE(cache_get(shared_memory, "user:1", value, &timestamp));
    EXPECT_STREQ("Alice", value);
    EXPECT_GT(timestamp, 0u);
}

TEST_F(CacheEngineTest, GetMissingKeyReturnsFalse) {
    char value[MAX_VAL_LEN] = {};
    uint64_t timestamp = 0;

    EXPECT_FALSE(
        cache_get(shared_memory, "missing:key", value, &timestamp)
    );
}

TEST_F(CacheEngineTest, PuttingSameKeyReplacesValue) {
    EXPECT_TRUE(cache_put(shared_memory, "user:1", "Alice"));
    EXPECT_TRUE(cache_put(shared_memory, "user:1", "Alicia"));

    char value[MAX_VAL_LEN] = {};
    EXPECT_TRUE(cache_get(shared_memory, "user:1", value, nullptr));

    EXPECT_STREQ("Alicia", value);

    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    EXPECT_EQ(1u, header->entry_count);
}

TEST_F(CacheEngineTest, UpdateChangesValue) {
    EXPECT_TRUE(cache_put(shared_memory, "user:1", "Alice"));
    EXPECT_TRUE(cache_update(shared_memory, "user:1", "Alicia"));

    char value[MAX_VAL_LEN] = {};
    EXPECT_TRUE(cache_get(shared_memory, "user:1", value, nullptr));

    EXPECT_STREQ("Alicia", value);
}

TEST_F(CacheEngineTest, UpdateMissingKeyReturnsFalse) {
    EXPECT_FALSE(
        cache_update(shared_memory, "missing:key", "value")
    );
}

TEST_F(CacheEngineTest, DeleteRemovesKey) {
    EXPECT_TRUE(cache_put(shared_memory, "user:1", "Alice"));
    EXPECT_TRUE(cache_delete(shared_memory, "user:1"));

    char value[MAX_VAL_LEN] = {};
    EXPECT_FALSE(
        cache_get(shared_memory, "user:1", value, nullptr)
    );

    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    EXPECT_EQ(0u, header->entry_count);
}

TEST_F(CacheEngineTest, DeleteMissingKeyReturnsFalse) {
    EXPECT_FALSE(
        cache_delete(shared_memory, "missing:key")
    );
}

TEST_F(CacheEngineTest, ConcurrentGetsCountEveryHitAndMissExactlyOnce) {
    ASSERT_TRUE(cache_put(shared_memory, "present", "value"));
    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    pthread_rwlock_wrlock(&header->rwlock);
    header->total_hits = 0;
    header->total_misses = 0;
    header->total_collisions = 0;
    pthread_rwlock_unlock(&header->rwlock);

    const uint32_t thread_count = 8;
    const uint32_t queries_per_thread = 100;
    std::atomic<uint32_t> lookup_failures(0);
    std::vector<std::thread> threads;
    for (uint32_t t = 0; t < thread_count; ++t) {
        threads.push_back(std::thread([this, queries_per_thread, &lookup_failures]() {
            char value[MAX_VAL_LEN] = {};
            for (uint32_t i = 0; i < queries_per_thread; ++i) {
                if (!cache_get(shared_memory, "present", value, nullptr) ||
                    cache_get(shared_memory, "absent", value, nullptr)) {
                    ++lookup_failures;
                }
            }
        }));
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(thread_count * queries_per_thread, header->total_hits);
    EXPECT_EQ(thread_count * queries_per_thread, header->total_misses);
    EXPECT_EQ(0u, lookup_failures.load());
}

TEST(CacheInitializationTest, InitializationPublishesAValidVersionedLayout) {
    const size_t size = sizeof(CacheHeader) + 2 * sizeof(CacheEntry);
    void* memory = mmap(
        nullptr,
        size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0
    );
    ASSERT_NE(MAP_FAILED, memory);
    std::memset(memory, 0, size);

    ASSERT_TRUE(cache_initialize_region(memory, size, 2));
    EXPECT_TRUE(cache_validate_layout(memory, size));
    EXPECT_FALSE(cache_validate_layout(memory, sizeof(CacheHeader)));

    CacheHeader* header = static_cast<CacheHeader*>(memory);
    EXPECT_EQ(CACHE_VERSION, header->version);
    EXPECT_EQ(2u, header->capacity);
    EXPECT_EQ(0u, header->entry_count);
    EXPECT_EQ(sizeof(CacheHeader), header->data_offset);
    pthread_rwlock_destroy(&header->rwlock);
    munmap(memory, size);
}
