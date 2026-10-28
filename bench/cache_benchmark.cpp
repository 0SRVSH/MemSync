#include "cache_engine.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <sys/mman.h>

namespace {

struct Request {
    char operation;
    char key[MAX_KEY_LEN];
    char value[MAX_VAL_LEN];
};

struct Response {
    int32_t status;
    char value[MAX_VAL_LEN];
};

struct Measurement {
    std::vector<double> latency_us;
    uint64_t failures;
    uint64_t operations;
    double seconds;

    Measurement() : failures(0), operations(0), seconds(0.0) {}
};

bool transfer_all(int fd, void* data, size_t size, bool sending) {
    char* bytes = static_cast<char*>(data);
    size_t transferred = 0;
    while (transferred < size) {
        ssize_t result = sending
            ? write(fd, bytes + transferred, size - transferred)
            : read(fd, bytes + transferred, size - transferred);
        if (result == -1 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return false;
        }
        transferred += static_cast<size_t>(result);
    }
    return true;
}

class LocalSocketCache {
public:
    explicit LocalSocketCache(uint32_t client_count) {
        try {
            for (uint32_t i = 0; i < client_count; ++i) {
                int sockets[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
                    throw std::runtime_error("socketpair failed");
                }
                clients_.push_back(sockets[0]);
                servers_.push_back(std::thread(
                    &LocalSocketCache::serve, this, sockets[1]
                ));
            }
        } catch (...) {
            shutdown_clients();
            throw;
        }
    }

    ~LocalSocketCache() {
        shutdown_clients();
    }

    bool request(
        uint32_t client,
        char operation,
        const char* key,
        const char* value,
        Response* response
    ) {
        Request request;
        std::memset(&request, 0, sizeof(request));
        request.operation = operation;
        std::strncpy(request.key, key, MAX_KEY_LEN - 1);
        if (value) {
            std::strncpy(request.value, value, MAX_VAL_LEN - 1);
        }
        return transfer_all(clients_[client], &request, sizeof(request), true) &&
               transfer_all(clients_[client], response, sizeof(*response), false);
    }

private:
    void serve(int fd) {
        Request request;
        while (transfer_all(fd, &request, sizeof(request), false)) {
            Response response;
            std::memset(&response, 0, sizeof(response));
            if (request.operation == 'Q') {
                break;
            }

            std::lock_guard<std::mutex> guard(mutex_);
            std::map<std::string, std::string>::iterator found =
                values_.find(request.key);
            if (request.operation == 'G') {
                if (found != values_.end()) {
                    response.status = 1;
                    std::strncpy(
                        response.value,
                        found->second.c_str(),
                        MAX_VAL_LEN - 1
                    );
                }
            } else if (request.operation == 'P') {
                values_[request.key] = request.value;
                response.status = 1;
            } else if (request.operation == 'U') {
                if (found != values_.end()) {
                    found->second = request.value;
                    response.status = 1;
                }
            } else {
                response.status = -1;
            }

            if (!transfer_all(fd, &response, sizeof(response), true)) {
                break;
            }
        }
        close(fd);
    }

    void shutdown_clients() {
        for (size_t i = 0; i < clients_.size(); ++i) {
            Request request;
            std::memset(&request, 0, sizeof(request));
            request.operation = 'Q';
            transfer_all(clients_[i], &request, sizeof(request), true);
            shutdown(clients_[i], SHUT_RDWR);
            close(clients_[i]);
        }
        clients_.clear();
        for (size_t i = 0; i < servers_.size(); ++i) {
            if (servers_[i].joinable()) {
                servers_[i].join();
            }
        }
        servers_.clear();
    }

    std::vector<int> clients_;
    std::vector<std::thread> servers_;
    std::map<std::string, std::string> values_;
    std::mutex mutex_;
};

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

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    size_t index = static_cast<size_t>(std::ceil(fraction * values.size())) - 1;
    return values[std::min(index, values.size() - 1)];
}

std::string csv_quote(const std::string& value) {
    std::string escaped = "\"";
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '"') {
            escaped += '"';
        }
        escaped += value[i];
    }
    escaped += '"';
    return escaped;
}

std::string cpu_model() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        const std::string key = "model name";
        if (line.compare(0, key.size(), key) == 0) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                return line.substr(colon + 2);
            }
        }
    }
    return "unknown";
}

bool parse_positive(const char* value, uint32_t* parsed) {
    char* end = nullptr;
    errno = 0;
    unsigned long number = std::strtoul(value, &end, 10);
    if (errno != 0 || !end || *end != '\0' || number == 0 ||
        number > UINT32_MAX) {
        return false;
    }
    *parsed = static_cast<uint32_t>(number);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    uint32_t operations = 2000;
    uint32_t trials = 3;
    for (int i = 1; i < argc; ++i) {
        if (i + 1 >= argc) {
            std::cerr << "Usage: memsync_benchmark [--operations N] [--trials N]\n";
            return 2;
        }
        uint32_t parsed = 0;
        if (!parse_positive(argv[i + 1], &parsed)) {
            std::cerr << "Benchmark counts must be positive 32-bit integers.\n";
            return 2;
        }
        if (std::strcmp(argv[i], "--operations") == 0) {
            operations = parsed;
        } else if (std::strcmp(argv[i], "--trials") == 0) {
            trials = parsed;
        } else {
            std::cerr << "Unknown option: " << argv[i] << '\n';
            return 2;
        }
        ++i;
    }

    struct utsname system_info;
    std::string system_name = "unknown";
    std::string system_release = "unknown";
    std::string architecture = "unknown";
    if (uname(&system_info) == 0) {
        system_name = system_info.sysname;
        system_release = system_info.release;
        architecture = system_info.machine;
    }
    std::cerr << "MemSync benchmark: " << system_name << ' ' << system_release
              << ", " << architecture << ", CPU: " << cpu_model()
              << ", compiler: " << __VERSION__
              << ", build: " << MEMSYNC_BUILD_TYPE
              << ", operations/worker: " << operations
              << ", trials: " << trials
              << ", warm-up: up to 100 operations per worker\n";

    char resource_name[96];
    std::snprintf(
        resource_name,
        sizeof(resource_name),
        "/memsync_bench_%ld",
        static_cast<long>(getpid())
    );
    shm_unlink(resource_name);
    CacheMapping mapping;
    if (!cache_open_shared(&mapping, true, resource_name)) {
        return 1;
    }

    const uint32_t payload_sizes[] = {16, 128, MAX_VAL_LEN - 1};
    const uint32_t thread_counts[] = {1, 4};
    const char* workloads[] = {"get_hit", "get_miss", "put_update", "mixed"};
    std::cout << "implementation,workload,payload_bytes,threads,trial,operations,"
                 "failures,median_us,p95_us,p99_us,throughput_ops_sec,os,release,"
                 "architecture,cpu,compiler,build_type,warmup_operations_per_worker\n";

    bool failed = false;
    for (size_t payload_index = 0;
         payload_index < sizeof(payload_sizes) / sizeof(payload_sizes[0]);
         ++payload_index) {
        const std::string payload(payload_sizes[payload_index], 'v');
        for (size_t workload_index = 0;
             workload_index < sizeof(workloads) / sizeof(workloads[0]);
             ++workload_index) {
            const std::string workload(workloads[workload_index]);
            for (size_t thread_index = 0;
                 thread_index < sizeof(thread_counts) / sizeof(thread_counts[0]);
                 ++thread_index) {
                const uint32_t thread_count = thread_counts[thread_index];
                for (uint32_t trial = 1; trial <= trials; ++trial) {
                    for (int implementation = 0; implementation < 2; ++implementation) {
                        const bool shared = implementation == 0;
                        Measurement measurement;
                        try {
                            if (shared) {
                                measurement = run_measurement(
                                    true, mapping.base, nullptr, workload, payload,
                                    thread_count, operations
                                );
                            } else {
                                LocalSocketCache socket_cache(thread_count);
                                measurement = run_measurement(
                                    false, nullptr, &socket_cache, workload, payload,
                                    thread_count, operations
                                );
                            }
                        } catch (const std::exception& error) {
                            std::cerr << "Benchmark setup failed: "
                                      << error.what() << '\n';
                            failed = true;
                            continue;
                        }

                        std::cout << (shared ? "shared_memory" : "unix_socket")
                                  << ',' << workload << ',' << payload.size()
                                  << ',' << thread_count << ',' << trial << ','
                                  << measurement.operations << ','
                                  << measurement.failures << ','
                                  << percentile(measurement.latency_us, 0.50) << ','
                                  << percentile(measurement.latency_us, 0.95) << ','
                                  << percentile(measurement.latency_us, 0.99) << ','
                                  << (measurement.seconds > 0.0
                                      ? measurement.operations / measurement.seconds
                                      : 0.0)
                                  << ',' << csv_quote(system_name)
                                  << ',' << csv_quote(system_release)
                                  << ',' << csv_quote(architecture)
                                  << ',' << csv_quote(cpu_model())
                                  << ',' << csv_quote(__VERSION__)
                                  << ',' << csv_quote(MEMSYNC_BUILD_TYPE)
                                  << ',' << std::min<uint32_t>(100, operations)
                                  << '\n';
                        std::cerr << (shared ? "shared_memory" : "unix_socket")
                                  << ' ' << workload << " payload=" << payload.size()
                                  << " threads=" << thread_count << " trial=" << trial
                                  << " operations=" << measurement.operations
                                  << " failures=" << measurement.failures
                                  << " median_us="
                                  << percentile(measurement.latency_us, 0.50)
                                  << " p95_us="
                                  << percentile(measurement.latency_us, 0.95)
                                  << " p99_us="
                                  << percentile(measurement.latency_us, 0.99)
                                  << " throughput_ops_sec="
                                  << (measurement.seconds > 0.0
                                      ? measurement.operations / measurement.seconds
                                      : 0.0)
                                  << '\n';
                        failed = failed || measurement.failures != 0;
                    }
                }
            }
        }
    }

    cache_close_shared(&mapping);
    if (shm_unlink(resource_name) == -1) {
        perror("Could not remove benchmark shared-memory cache");
        return 1;
    }
    return failed ? 1 : 0;
}
