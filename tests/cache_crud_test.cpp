#include "cache_test_fixture.h"

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
