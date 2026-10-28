#include "cache_engine.h"

#include <cerrno>
#include <cstdio>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <gtest/gtest.h>

namespace {

class SharedMemoryCleanup {
public:
    explicit SharedMemoryCleanup(const std::string& name) : name_(name) {}
    ~SharedMemoryCleanup() {
        shm_unlink(name_.c_str());
    }

private:
    std::string name_;
};

std::string unique_shared_memory_name(const char* label) {
    char name[96];
    std::snprintf(
        name,
        sizeof(name),
        "/memsync_%s_%ld",
        label,
        static_cast<long>(getpid())
    );
    return std::string(name);
}

bool open_with_retry(
    CacheMapping& mapping,
    const std::string& name
) {
    for (uint32_t attempt = 0; attempt < 100; ++attempt) {
        if (cache_open_shared(&mapping, true, name.c_str())) {
            return true;
        }
        usleep(1000);
    }
    return false;
}

} // namespace

TEST(CacheInitializationTest, ConcurrentProcessesPublishOneUsableCache) {
    const std::string name = unique_shared_memory_name("startup");
    shm_unlink(name.c_str());
    SharedMemoryCleanup cleanup(name);

    int result_pipe[2];
    ASSERT_EQ(0, pipe(result_pipe));
    const uint32_t process_count = 4;
    std::vector<pid_t> children;

    for (uint32_t i = 0; i < process_count; ++i) {
        pid_t child = fork();
        if (child == -1) {
            break;
        }
        if (child == 0) {
            close(result_pipe[0]);
            CacheMapping mapping;
            bool opened = open_with_retry(mapping, name);
            char result = 'F';
            bool stored = false;
            if (opened) {
                char key[32];
                std::snprintf(key, sizeof(key), "worker-%u", i);
                stored = cache_put(mapping.base, key, "ready");
                result = mapping.created ? 'C' : 'A';
                cache_close_shared(&mapping);
            }
            if (!stored) {
                result = 'F';
            }
            write(result_pipe[1], &result, sizeof(result));
            close(result_pipe[1]);
            _exit(stored ? 0 : 1);
        }
        children.push_back(child);
    }

    close(result_pipe[1]);
    bool children_ok = children.size() == process_count;
    for (size_t i = 0; i < children.size(); ++i) {
        int status = 0;
        if (waitpid(children[i], &status, 0) == -1 ||
            !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            children_ok = false;
        }
    }

    std::vector<char> results;
    while (results.size() < children.size()) {
        char result = '\0';
        ssize_t bytes_read = read(result_pipe[0], &result, sizeof(result));
        if (bytes_read == -1 && errno == EINTR) {
            continue;
        }
        if (bytes_read != sizeof(result)) {
            break;
        }
        results.push_back(result);
    }
    close(result_pipe[0]);

    bool cache_valid = false;
    uint32_t entry_count = 0;
    CacheMapping mapping;
    if (cache_open_shared(&mapping, false, name.c_str())) {
        CacheHeader* header = static_cast<CacheHeader*>(mapping.base);
        pthread_rwlock_rdlock(&header->rwlock);
        cache_valid = cache_validate_layout(mapping.base, mapping.size);
        entry_count = header->entry_count;
        pthread_rwlock_unlock(&header->rwlock);
        cache_close_shared(&mapping);
    }
    uint32_t creators = 0;
    for (char child_result : results) {
        if (child_result == 'C') {
            ++creators;
        } else if (child_result != 'A') {
            children_ok = false;
        }
    }
    EXPECT_TRUE(children_ok);
    EXPECT_EQ(process_count, results.size());
    EXPECT_EQ(1u, creators);
    EXPECT_TRUE(cache_valid);
    EXPECT_EQ(process_count, entry_count);
}

TEST(CacheInitializationTest, AttachingToPartialCacheFailsBeforeLockUse) {
    const std::string name = unique_shared_memory_name("partial");
    shm_unlink(name.c_str());
    SharedMemoryCleanup cleanup(name);

    int fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    ASSERT_NE(-1, fd);
    int truncate_result = ftruncate(
        fd,
        static_cast<off_t>(CACHE_SHARED_MEMORY_SIZE)
    );
    close(fd);
    ASSERT_EQ(0, truncate_result);

    CacheMapping mapping;
    bool opened = cache_open_shared(&mapping, false, name.c_str());
    EXPECT_FALSE(opened);
}

TEST(CacheInitializationTest, ReopeningValidCachePreservesExistingRecords) {
    const std::string name = unique_shared_memory_name("preserve");
    shm_unlink(name.c_str());
    SharedMemoryCleanup cleanup(name);

    CacheMapping mapping;
    ASSERT_TRUE(cache_open_shared(&mapping, true, name.c_str()));
    ASSERT_TRUE(mapping.created);
    ASSERT_TRUE(cache_put(mapping.base, "existing", "retained"));
    cache_close_shared(&mapping);

    ASSERT_TRUE(cache_open_shared(&mapping, true, name.c_str()));
    EXPECT_FALSE(mapping.created);
    char value[MAX_VAL_LEN] = {};
    EXPECT_TRUE(cache_get(mapping.base, "existing", value, nullptr));
    EXPECT_STREQ("retained", value);
    cache_close_shared(&mapping);
}
