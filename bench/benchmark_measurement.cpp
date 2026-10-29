#include "benchmark.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>

namespace benchmark {
namespace {

bool perform_shared(
    void* base,
    char operation,
    const char* key,
    const char* value,
    char* out_value
) {
    if (operation == 'G') {
        return cache_get(base, key, out_value, nullptr);
    }
    if (operation == 'P') {
        return cache_put(base, key, value);
    }
    return cache_update(base, key, value);
}

bool perform_socket(
    LocalSocketCache& cache,
    uint32_t worker,
    char operation,
    const char* key,
    const char* value,
    char* out_value,
    bool* operation_succeeded
) {
    Response response;
    std::memset(&response, 0, sizeof(response));
    if (!cache.request(worker, operation, key, value, &response)) {
        return false;
    }
    if (response.status < 0) {
        return false;
    }
    *operation_succeeded = response.status == 1;
    if (operation == 'G' && *operation_succeeded) {
        std::strncpy(out_value, response.value, MAX_VAL_LEN - 1);
    }
    return true;
}

bool expected_result(
    const std::string& workload,
    uint32_t index,
    char* operation,
    const char** key,
    const char** value
) {
    *value = nullptr;
    if (workload == "get_hit") {
        *operation = 'G';
        *key = "bench-hit";
    } else if (workload == "get_miss") {
        *operation = 'G';
        *key = "bench-miss";
    } else if (workload == "put_update") {
        *operation = (index % 2 == 0) ? 'P' : 'U';
        *key = (index % 2 == 0) ? "bench-put" : "bench-update";
    } else {
        switch (index % 4) {
        case 0:
            *operation = 'G';
            *key = "bench-hit";
            break;
        case 1:
            *operation = 'G';
            *key = "bench-miss";
            break;
        case 2:
            *operation = 'P';
            *key = "bench-put";
            break;
        default:
            *operation = 'U';
            *key = "bench-update";
            break;
        }
    }
    return true;
}

bool prepare_shared(void* base, const std::string& payload) {
    return cache_put(base, "bench-hit", payload.c_str()) &&
           cache_put(base, "bench-update", payload.c_str());
}

bool prepare_socket(LocalSocketCache& cache, const std::string& payload) {
    Response response;
    return cache.request(0, 'P', "bench-hit", payload.c_str(), &response) &&
           response.status == 1 &&
           cache.request(0, 'P', "bench-update", payload.c_str(), &response) &&
           response.status == 1;
}

}

Measurement run_measurement(
    bool shared,
    void* base,
    LocalSocketCache* socket_cache,
    const std::string& workload,
    const std::string& payload,
    uint32_t thread_count,
    uint32_t operations_per_thread
) {
    Measurement result;
    const uint32_t warmup = std::min<uint32_t>(100, operations_per_thread);

    if (shared) {
        if (!prepare_shared(base, payload)) {
            result.failures = 1;
            return result;
        }
    } else if (!prepare_socket(*socket_cache, payload)) {
        result.failures = 1;
        return result;
    }

    for (uint32_t worker = 0; worker < thread_count; ++worker) {
        for (uint32_t index = 0; index < warmup; ++index) {
            char operation;
            const char* key;
            const char* value;
            expected_result(workload, index + worker, &operation, &key, &value);
            char out_value[MAX_VAL_LEN] = {};
            bool operation_succeeded = false;
            bool request_completed = true;
            if (shared) {
                operation_succeeded = perform_shared(
                    base, operation, key, payload.c_str(), out_value
                );
            } else {
                request_completed = perform_socket(
                    *socket_cache, worker, operation, key, payload.c_str(),
                    out_value, &operation_succeeded
                );
            }
            const bool should_succeed = operation != 'G' ||
                std::strcmp(key, "bench-hit") == 0;
            bool correct = request_completed &&
                           operation_succeeded == should_succeed;
            if (correct && operation == 'G' && should_succeed) {
                correct = std::strcmp(out_value, payload.c_str()) == 0;
            }
            if (!correct) {
                result.failures = 1;
                return result;
            }
        }
    }

    std::vector<std::vector<double> > latencies(thread_count);
    std::vector<uint64_t> failures(thread_count, 0);
    std::vector<std::thread> workers;
    std::mutex ready_mutex;
    std::condition_variable ready_condition;
    uint32_t ready_count = 0;
    bool start = false;

    for (uint32_t worker = 0; worker < thread_count; ++worker) {
        latencies[worker].reserve(operations_per_thread);
        workers.push_back(std::thread([&, worker]() {
            {
                std::unique_lock<std::mutex> lock(ready_mutex);
                ++ready_count;
                ready_condition.notify_all();
                ready_condition.wait(lock, [&]() { return start; });
            }
            for (uint32_t index = 0; index < operations_per_thread; ++index) {
                char operation;
                const char* key;
                const char* value;
                expected_result(workload, index + worker, &operation, &key, &value);
                char out_value[MAX_VAL_LEN] = {};
                const std::chrono::steady_clock::time_point begin =
                    std::chrono::steady_clock::now();
                bool operation_succeeded = false;
                bool request_completed = true;
                if (shared) {
                    operation_succeeded = perform_shared(
                        base, operation, key, payload.c_str(), out_value
                    );
                } else {
                    request_completed = perform_socket(
                        *socket_cache, worker, operation, key, payload.c_str(),
                        out_value, &operation_succeeded
                    );
                }
                const std::chrono::steady_clock::time_point end =
                    std::chrono::steady_clock::now();
                latencies[worker].push_back(
                    std::chrono::duration<double, std::micro>(end - begin).count()
                );

                const bool should_succeed = operation != 'G' ||
                    std::strcmp(key, "bench-hit") == 0;
                bool correct = request_completed &&
                               operation_succeeded == should_succeed;
                if (correct && operation == 'G' && should_succeed) {
                    correct = std::strcmp(out_value, payload.c_str()) == 0;
                }
                if (!correct) {
                    ++failures[worker];
                }
            }
        }));
    }

    {
        std::unique_lock<std::mutex> lock(ready_mutex);
        ready_condition.wait(lock, [&]() { return ready_count == thread_count; });
    }
    const std::chrono::steady_clock::time_point begin =
        std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(ready_mutex);
        start = true;
    }
    ready_condition.notify_all();
    for (size_t i = 0; i < workers.size(); ++i) {
        workers[i].join();
    }
    const std::chrono::steady_clock::time_point end =
        std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(end - begin).count();
    result.operations = static_cast<uint64_t>(thread_count) * operations_per_thread;
    for (uint32_t worker = 0; worker < thread_count; ++worker) {
        result.failures += failures[worker];
        result.latency_us.insert(
            result.latency_us.end(),
            latencies[worker].begin(),
            latencies[worker].end()
        );
    }
    return result;
}

}
