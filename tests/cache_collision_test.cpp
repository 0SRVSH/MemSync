#include "cache_test_fixture.h"

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
