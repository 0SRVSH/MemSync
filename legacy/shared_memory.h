#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H
#include<cstddef> //for using size_t 
#include<semaphore.h> // posix semaphore library
#include<string>
struct MemSyncHeader{
    size_t total_bytes; //total allocated memory size
    size_t slot_capacity; //total key value slots in cache
    size_t active_items; //how many slots currently hold key value pairs
};
class SharedMemoryManager{
    private:
    std::string shm_name;
    std::string sem_name;
    size_t shm_size;
    int shm_fd; //file descriptor returned by shm_open()
    void* mapped_ptr; //address of the start of virtual memory
    sem_t* semaphore; 
    bool is_owner; //server ->true, client->false
    public:
    SharedMemoryManager(const std::string &shm_n, const std::string &sem_n, size_t size);
    ~SharedMemoryManager();
    bool init_as_server();
    bool attach_as_client();
    bool lock();    
    bool unlock();
    void *get_base_ptr();
    void cleanup();
};
#endif
