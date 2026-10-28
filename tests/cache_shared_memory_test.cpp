#include "cache_engine.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <string>
#include <sys/mman.h>
#include <sys/poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
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

bool wait_for_child(pid_t child, int* status, uint32_t timeout_ms) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        pid_t result = waitpid(child, status, WNOHANG);
        if (result == child) {
            return true;
        }
        if (result == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    kill(child, SIGKILL);
    while (waitpid(child, status, 0) == -1 && errno == EINTR) {
    }
    return false;
}

bool read_byte_with_timeout(int fd, char* result, int timeout_ms) {
    struct pollfd descriptor;
    descriptor.fd = fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    int ready;
    do {
        ready = poll(&descriptor, 1, timeout_ms);
    } while (ready == -1 && errno == EINTR);
    return ready > 0 && (descriptor.revents & POLLIN) != 0 &&
           read(fd, result, sizeof(*result)) == sizeof(*result);
}

std::vector<std::string> colliding_keys(uint32_t count) {
    std::vector<std::string> keys;
    uint32_t target_hash = 0;
    for (uint32_t candidate = 0; keys.size() < count; ++candidate) {
        char key[64];
        std::snprintf(key, sizeof(key), "integration-collision-%u", candidate);
        uint32_t candidate_hash = hash_key(key, CACHE_DEFAULT_CAPACITY);
        if (keys.empty()) {
            target_hash = candidate_hash;
        }
        if (candidate_hash == target_hash) {
            keys.push_back(key);
        }
    }
    return keys;
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
        if (!wait_for_child(children[i], &status, 5000) ||
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

TEST(CacheProcessIntegrationTest, AttacherDoesNotCreateMissingCache) {
    const std::string name = unique_shared_memory_name("missing");
    shm_unlink(name.c_str());
    SharedMemoryCleanup cleanup(name);

    CacheMapping mapping;
    EXPECT_FALSE(cache_open_shared(&mapping, false, name.c_str()));
    EXPECT_EQ(-1, shm_open(name.c_str(), O_RDWR, 0600));
    EXPECT_EQ(ENOENT, errno);
    EXPECT_EQ(-1, shm_unlink(name.c_str()));
    EXPECT_EQ(ENOENT, errno);
    EXPECT_EQ(-1, shm_unlink(name.c_str()));
    EXPECT_EQ(ENOENT, errno);
}

TEST(CacheProcessIntegrationTest, IndependentProcessReadsValueAndTtlMetadata) {
    const std::string name = unique_shared_memory_name("visibility");
    shm_unlink(name.c_str());
    SharedMemoryCleanup cleanup(name);

    CacheMapping owner;
    ASSERT_TRUE(cache_open_shared(&owner, true, name.c_str()));
    ASSERT_TRUE(cache_put_ttl(owner.base, "shared-key", "shared-value", 30));
    char expected_value[MAX_VAL_LEN] = {};
    uint64_t expected_timestamp = 0;
    ASSERT_TRUE(cache_get(owner.base, "shared-key", expected_value, &expected_timestamp));

    int result_pipe[2];
    ASSERT_EQ(0, pipe(result_pipe));
    pid_t child = fork();
    ASSERT_NE(-1, child);
    if (child == 0) {
        close(result_pipe[0]);
        CacheMapping client;
        char value[MAX_VAL_LEN] = {};
        uint64_t timestamp = 0;
        bool ok = cache_open_shared(&client, false, name.c_str()) &&
                  cache_get(client.base, "shared-key", value, &timestamp) &&
                  std::strcmp(value, "shared-value") == 0 &&
                  timestamp == expected_timestamp;
        char result = ok ? 'P' : 'F';
        write(result_pipe[1], &result, sizeof(result));
        if (client.base) {
            cache_close_shared(&client);
        }
        close(result_pipe[1]);
        _exit(ok ? 0 : 1);
    }

    close(result_pipe[1]);
    int status = 0;
    bool completed = wait_for_child(child, &status, 5000);
    char result = '\0';
    bool received = read_byte_with_timeout(result_pipe[0], &result, 1000);
    close(result_pipe[0]);
    EXPECT_TRUE(completed);
    ASSERT_TRUE(completed);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(0, WEXITSTATUS(status));
    EXPECT_TRUE(received);
    EXPECT_EQ('P', result);
    cache_close_shared(&owner);
}

TEST(CacheProcessIntegrationTest, ConcurrentProcessesKeepOperationsConsistent) {
    const std::string name = unique_shared_memory_name("operations");
    shm_unlink(name.c_str());
    SharedMemoryCleanup cleanup(name);

    CacheMapping owner;
    ASSERT_TRUE(cache_open_shared(&owner, true, name.c_str()));

    int result_pipe[2];
    ASSERT_EQ(0, pipe(result_pipe));
    const uint32_t process_count = 4;
    const uint32_t iterations = 25;
    const std::vector<std::string> keys = colliding_keys(process_count);
    std::vector<pid_t> children;
    for (uint32_t i = 0; i < process_count; ++i) {
        pid_t child = fork();
        if (child == -1) {
            break;
        }
        if (child == 0) {
            close(result_pipe[0]);
            CacheMapping client;
            bool ok = cache_open_shared(&client, false, name.c_str());
            const char* key = keys[i].c_str();
            char value[MAX_VAL_LEN] = {};
            for (uint32_t iteration = 0; ok && iteration < iterations; ++iteration) {
                ok = cache_put(client.base, key, "first") &&
                     cache_get(client.base, key, value, nullptr) &&
                     std::strcmp(value, "first") == 0 &&
                     cache_update(client.base, key, "second") &&
                     cache_get(client.base, key, value, nullptr) &&
                     std::strcmp(value, "second") == 0 &&
                     cache_delete(client.base, key);
            }
            if (ok) {
                ok = !cache_get(client.base, key, value, nullptr);
            }
            char result = ok ? 'P' : 'F';
            write(result_pipe[1], &result, sizeof(result));
            if (client.base) {
                cache_close_shared(&client);
            }
            close(result_pipe[1]);
            _exit(ok ? 0 : 1);
        }
        children.push_back(child);
    }

    close(result_pipe[1]);
    bool children_ok = children.size() == process_count;
    for (size_t i = 0; i < children.size(); ++i) {
        int status = 0;
        if (!wait_for_child(children[i], &status, 5000) ||
            !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            children_ok = false;
        }
    }

    std::vector<char> results;
    for (size_t i = 0; i < children.size(); ++i) {
        char result = '\0';
        if (read_byte_with_timeout(result_pipe[0], &result, 1000)) {
            results.push_back(result);
        }
    }
    close(result_pipe[0]);

    CacheHeader* header = static_cast<CacheHeader*>(owner.base);
    ASSERT_EQ(0, pthread_rwlock_rdlock(&header->rwlock));
    uint32_t entry_count = header->entry_count;
    uint64_t hits = header->total_hits;
    uint64_t misses = header->total_misses;
    uint64_t collisions = header->total_collisions;
    pthread_rwlock_unlock(&header->rwlock);
    EXPECT_TRUE(children_ok);
    EXPECT_EQ(process_count, results.size());
    for (char result : results) {
        EXPECT_EQ('P', result);
    }
    EXPECT_EQ(0u, entry_count);
    EXPECT_EQ(process_count * iterations * 2, hits);
    EXPECT_EQ(process_count, misses);
    EXPECT_GT(collisions, 0u);
    cache_close_shared(&owner);
}

TEST(CacheProcessIntegrationTest, ConcurrentInsertsRespectFullCapacity) {
    const std::string name = unique_shared_memory_name("full");
    shm_unlink(name.c_str());
    SharedMemoryCleanup cleanup(name);

    CacheMapping owner;
    ASSERT_TRUE(cache_open_shared(&owner, true, name.c_str()));

    int result_pipe[2];
    ASSERT_EQ(0, pipe(result_pipe));
    const uint32_t process_count = 4;
    const uint32_t entries_per_process = CACHE_DEFAULT_CAPACITY / process_count;
    std::vector<pid_t> children;
    for (uint32_t process = 0; process < process_count; ++process) {
        pid_t child = fork();
        if (child == -1) {
            break;
        }
        if (child == 0) {
            close(result_pipe[0]);
            CacheMapping client;
            bool ok = cache_open_shared(&client, false, name.c_str());
            char key[64];
            for (uint32_t entry = 0; ok && entry < entries_per_process; ++entry) {
                std::snprintf(
                    key,
                    sizeof(key),
                    "capacity-%u-%u",
                    process,
                    entry
                );
                ok = cache_put(client.base, key, "value");
            }
            char result = ok ? 'P' : 'F';
            write(result_pipe[1], &result, sizeof(result));
            if (client.base) {
                cache_close_shared(&client);
            }
            close(result_pipe[1]);
            _exit(ok ? 0 : 1);
        }
        children.push_back(child);
    }

    close(result_pipe[1]);
    bool children_ok = children.size() == process_count;
    for (size_t i = 0; i < children.size(); ++i) {
        int status = 0;
        if (!wait_for_child(children[i], &status, 10000) ||
            !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            children_ok = false;
        }
    }
    std::vector<char> results;
    for (size_t i = 0; i < children.size(); ++i) {
        char result = '\0';
        if (read_byte_with_timeout(result_pipe[0], &result, 1000)) {
            results.push_back(result);
        }
    }
    close(result_pipe[0]);

    CacheHeader* header = static_cast<CacheHeader*>(owner.base);
    ASSERT_EQ(0, pthread_rwlock_rdlock(&header->rwlock));
    uint32_t entry_count = header->entry_count;
    pthread_rwlock_unlock(&header->rwlock);
    EXPECT_TRUE(children_ok);
    EXPECT_EQ(process_count, results.size());
    for (char result : results) {
        EXPECT_EQ('P', result);
    }
    EXPECT_EQ(CACHE_DEFAULT_CAPACITY, entry_count);
    EXPECT_TRUE(cache_put(owner.base, "one-past-capacity", "new"));
    ASSERT_EQ(0, pthread_rwlock_rdlock(&header->rwlock));
    EXPECT_EQ(CACHE_DEFAULT_CAPACITY, header->entry_count);
    pthread_rwlock_unlock(&header->rwlock);
    char value[MAX_VAL_LEN] = {};
    EXPECT_TRUE(cache_get(owner.base, "one-past-capacity", value, nullptr));
    EXPECT_STREQ("new", value);
    cache_close_shared(&owner);
}

TEST(CacheProcessIntegrationTest, UnlinkDoesNotInvalidateExistingMappings) {
    const std::string name = unique_shared_memory_name("unlink");
    shm_unlink(name.c_str());
    SharedMemoryCleanup cleanup(name);

    CacheMapping owner;
    ASSERT_TRUE(cache_open_shared(&owner, true, name.c_str()));
    ASSERT_TRUE(cache_put(owner.base, "kept", "mapped"));

    int ready_pipe[2];
    int release_pipe[2];
    ASSERT_EQ(0, pipe(ready_pipe));
    ASSERT_EQ(0, pipe(release_pipe));
    pid_t child = fork();
    ASSERT_NE(-1, child);
    if (child == 0) {
        close(ready_pipe[0]);
        close(release_pipe[1]);
        CacheMapping client;
        bool opened = cache_open_shared(&client, false, name.c_str());
        char ready = opened ? 'R' : 'F';
        write(ready_pipe[1], &ready, sizeof(ready));
        char release = '\0';
        bool continued = read(release_pipe[0], &release, sizeof(release)) ==
                         sizeof(release);
        char value[MAX_VAL_LEN] = {};
        bool found = opened && continued && release == 'G' &&
                     cache_get(client.base, "kept", value, nullptr) &&
                     std::strcmp(value, "mapped") == 0;
        char result = found ? 'P' : 'F';
        write(ready_pipe[1], &result, sizeof(result));
        if (client.base) {
            cache_close_shared(&client);
        }
        close(ready_pipe[1]);
        close(release_pipe[0]);
        _exit(found ? 0 : 1);
    }

    close(ready_pipe[1]);
    close(release_pipe[0]);
    char ready = '\0';
    ASSERT_TRUE(read_byte_with_timeout(ready_pipe[0], &ready, 5000));
    ASSERT_EQ('R', ready);
    cache_close_shared(&owner);
    ASSERT_EQ(0, shm_unlink(name.c_str()));
    char release = 'G';
    ASSERT_EQ(sizeof(release), write(release_pipe[1], &release, sizeof(release)));
    close(release_pipe[1]);

    int status = 0;
    bool completed = wait_for_child(child, &status, 5000);
    char result = '\0';
    bool received = read_byte_with_timeout(ready_pipe[0], &result, 1000);
    close(ready_pipe[0]);
    EXPECT_TRUE(completed);
    ASSERT_TRUE(completed);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(0, WEXITSTATUS(status));
    EXPECT_TRUE(received);
    EXPECT_EQ('P', result);

    EXPECT_EQ(-1, shm_open(name.c_str(), O_RDWR, 0600));
    EXPECT_EQ(ENOENT, errno);
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

TEST(CacheInitializationTest, AttachingToWrongSizedCacheFailsSafely) {
    const off_t invalid_sizes[] = {
        static_cast<off_t>(sizeof(CacheHeader) - 1),
        static_cast<off_t>(CACHE_SHARED_MEMORY_SIZE + 4096)
    };
    const char* labels[] = {"undersized", "oversized"};
    for (size_t i = 0; i < 2; ++i) {
        const std::string name = unique_shared_memory_name(labels[i]);
        shm_unlink(name.c_str());
        SharedMemoryCleanup cleanup(name);
        int fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        ASSERT_NE(-1, fd);
        ASSERT_EQ(0, ftruncate(fd, invalid_sizes[i]));
        close(fd);

        CacheMapping mapping;
        EXPECT_FALSE(cache_open_shared(&mapping, false, name.c_str()));
    }
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
