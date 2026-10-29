#include "benchmark.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace benchmark {

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

}
