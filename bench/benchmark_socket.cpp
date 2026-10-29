#include "benchmark.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace benchmark {
namespace {

struct Request {
    char operation;
    char key[MAX_KEY_LEN];
    char value[MAX_VAL_LEN];
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

}

LocalSocketCache::LocalSocketCache(uint32_t client_count) {
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

LocalSocketCache::~LocalSocketCache() {
    shutdown_clients();
}

bool LocalSocketCache::request(
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

void LocalSocketCache::serve(int fd) {
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

void LocalSocketCache::shutdown_clients() {
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

}
