#ifndef CACHE_TEST_FIXTURE_H
#define CACHE_TEST_FIXTURE_H

#include <gtest/gtest.h>
#include "cache_engine.h"

#include <cstring>
#include <sys/mman.h>

class CacheEngineTest : public ::testing::Test {
protected:
    static constexpr uint32_t capacity = 8;
    const size_t memory_size =
        sizeof(CacheHeader) + capacity * sizeof(CacheEntry);

    void* shared_memory = MAP_FAILED;
    bool lock_initialized = false;

    void SetUp() override {
        shared_memory = mmap(
            nullptr,
            memory_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS,
            -1,
            0
        );
        ASSERT_NE(MAP_FAILED, shared_memory);

        std::memset(shared_memory, 0, memory_size);

        CacheHeader* header = static_cast<CacheHeader*>(shared_memory);
        header->magic = CACHE_MAGIC;
        header->capacity = capacity;
        header->entry_count = 0;
        header->data_offset = sizeof(CacheHeader);

        ASSERT_TRUE(cache_init_lock(header));
        lock_initialized = true;
    }

    void TearDown() override {
        if (shared_memory != MAP_FAILED) {
            if (lock_initialized) {
                CacheHeader* header =
                    static_cast<CacheHeader*>(shared_memory);
                pthread_rwlock_destroy(&header->rwlock);
            }
            munmap(shared_memory, memory_size);
        }
    }
};

#endif
