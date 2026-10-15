#include "cache_test_fixture.h"

#include <unistd.h>

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
