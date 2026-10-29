#ifndef MEMSYNC_BENCHMARK_H
#define MEMSYNC_BENCHMARK_H

#include "cache_engine.h"

#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace benchmark {

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

class LocalSocketCache {
public:
    explicit LocalSocketCache(uint32_t client_count);
    ~LocalSocketCache();

    bool request(
        uint32_t client,
        char operation,
        const char* key,
        const char* value,
        Response* response
    );

private:
    void serve(int fd);
    void shutdown_clients();

    std::vector<int> clients_;
    std::vector<std::thread> servers_;
    std::map<std::string, std::string> values_;
    std::mutex mutex_;
};

Measurement run_measurement(
    bool shared,
    void* base,
    LocalSocketCache* socket_cache,
    const std::string& workload,
    const std::string& payload,
    uint32_t thread_count,
    uint32_t operations_per_thread
);

double percentile(std::vector<double> values, double fraction);
std::string csv_quote(const std::string& value);
std::string cpu_model();

}

#endif
