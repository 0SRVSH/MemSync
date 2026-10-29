#include "benchmark.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace {

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

}

int main(int argc, char** argv) {
    using namespace benchmark;

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
