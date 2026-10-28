#include "cache_test_fixture.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
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

TEST_F(CacheEngineTest, RejectsNullAndEmptyKeysAndNullValues) {
    char value[MAX_VAL_LEN] = {};
    EXPECT_FALSE(cache_put(shared_memory, nullptr, "value"));
    EXPECT_FALSE(cache_put(shared_memory, "", "value"));
    EXPECT_FALSE(cache_put(shared_memory, "key", nullptr));
    EXPECT_FALSE(cache_get(shared_memory, nullptr, value, nullptr));
    EXPECT_FALSE(cache_get(shared_memory, "", value, nullptr));
    EXPECT_FALSE(cache_get(shared_memory, "key", nullptr, nullptr));
    EXPECT_FALSE(cache_update(shared_memory, nullptr, "value"));
    EXPECT_FALSE(cache_update(shared_memory, "key", nullptr));
    EXPECT_FALSE(cache_delete(shared_memory, ""));
    EXPECT_EQ(-1, find_slot(shared_memory, ""));
    EXPECT_EQ(0u, static_cast<CacheHeader*>(shared_memory)->entry_count);
}

TEST_F(CacheEngineTest, AcceptsMaximumLengthKeyAndValueAndEmptyValue) {
    const std::string maximum_key(MAX_KEY_LEN - 1, 'k');
    const std::string maximum_value(MAX_VAL_LEN - 1, 'v');
    ASSERT_TRUE(cache_put(shared_memory, maximum_key.c_str(), maximum_value.c_str()));

    char value[MAX_VAL_LEN] = {};
    ASSERT_TRUE(cache_get(shared_memory, maximum_key.c_str(), value, nullptr));
    EXPECT_STREQ(maximum_value.c_str(), value);
    EXPECT_FALSE(cache_put(shared_memory, (maximum_key + "k").c_str(), "value"));
    EXPECT_FALSE(cache_put(shared_memory, "too-long-value", (maximum_value + "v").c_str()));

    ASSERT_TRUE(cache_put(shared_memory, "empty-value", ""));
    std::memset(value, 'x', sizeof(value));
    ASSERT_TRUE(cache_get(shared_memory, "empty-value", value, nullptr));
    EXPECT_STREQ("", value);
}

TEST_F(CacheEngineTest, RepeatedDeleteAndInsertCyclesPreserveCount) {
    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    for (uint32_t i = 0; i < 50; ++i) {
        ASSERT_TRUE(cache_put(shared_memory, "cycle", "value"));
        EXPECT_EQ(1u, header->entry_count);
        ASSERT_TRUE(cache_delete(shared_memory, "cycle"));
        EXPECT_EQ(0u, header->entry_count);
    }
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

TEST_F(CacheEngineTest, ConcurrentMutationsPreserveCacheInvariants) {
    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    const uint32_t thread_count = 4;
    const uint32_t iterations = 100;
    std::atomic<uint32_t> operation_failures(0);
    std::vector<std::thread> threads;

    for (uint32_t t = 0; t < thread_count; ++t) {
        threads.push_back(std::thread(
            [this, t, iterations, &operation_failures]() {
                char key[16];
                std::snprintf(key, sizeof(key), "thread-%u", t);
                char value[MAX_VAL_LEN] = {};
                for (uint32_t i = 0; i < iterations; ++i) {
                    if (!cache_put(shared_memory, key, "first") ||
                        !cache_get(shared_memory, key, value, nullptr) ||
                        !cache_update(shared_memory, key, "second") ||
                        !cache_get(shared_memory, key, value, nullptr) ||
                        !cache_delete(shared_memory, key)) {
                        ++operation_failures;
                    }
                }
            }
        ));
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(0u, operation_failures.load());
    EXPECT_EQ(0u, header->entry_count);
    EXPECT_EQ(thread_count * iterations * 2, header->total_hits);
    for (uint32_t i = 0; i < header->capacity; ++i) {
        EXPECT_NE(SlotState::OCCUPIED, get_entry_ptr(shared_memory, i)->state);
    }
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
