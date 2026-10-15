#include "cache_test_fixture.h"

TEST_F(CacheEngineTest, InsertingPastCapacityEvictsOneEntry) {
    const char* keys[] = {
        "key-0", "key-1", "key-2", "key-3",
        "key-4", "key-5", "key-6", "key-7"
    };

    for (uint32_t i = 0; i < capacity; ++i) {
        EXPECT_TRUE(cache_put(shared_memory, keys[i], "old-value"));
    }

    EXPECT_TRUE(cache_put(shared_memory, "new-key", "new-value"));

    CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
    EXPECT_EQ(capacity, header->entry_count);

    char value[MAX_VAL_LEN] = {};
    EXPECT_TRUE(cache_get(shared_memory, "new-key", value, nullptr));
    EXPECT_STREQ("new-value", value);

    uint32_t remaining_old_keys = 0;
    for (uint32_t i = 0; i < capacity; ++i) {
        if (cache_get(shared_memory, keys[i], value, nullptr)) {
            ++remaining_old_keys;
        }
    }

    EXPECT_EQ(capacity - 1, remaining_old_keys);
}
