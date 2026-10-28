#include "shared_memory.h"
#include<sys/mman.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>
#include<cerrno>
using namespace std;
SharedMemoryManager::SharedMemoryManager(const string &shm_n, const string &sem_n, size_t size){
    shm_name = shm_n;
    sem_name = sem_n;
    shm_size = size;
    shm_fd = -1;
    mapped_ptr = nullptr;
    semaphore = nullptr;
    is_owner = false;
}

SharedMemoryManager::~SharedMemoryManager(){
    cleanup();
}

bool SharedMemoryManager:: init_as_server(){
    is_owner = true;
    //creating shared meoery object in RAM
    shm_fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    if(shm_fd == -1)
    return false;
    if(ftruncate(shm_fd, shm_size) == -1){
        cleanup();
        return false;
    }
     //map the RAM into virtual address space of the processes
    mapped_ptr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(mapped_ptr == MAP_FAILED){
        mapped_ptr = nullptr;
        cleanup();
        return false;
    }
    //create named semaphore lock
    semaphore = sem_open(sem_name.c_str(), O_CREAT| O_RDWR, 0666, 1);
    if(semaphore == SEM_FAILED){
        semaphore = nullptr;
        cleanup();
        return false;
    }
    return true;
}

bool SharedMemoryManager :: attach_as_client(){
    is_owner = false;
    shm_fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
    if(shm_fd == -1)
    return false;
    mapped_ptr = mmap(NULL, shm_size, PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(mapped_ptr == MAP_FAILED){
        mapped_ptr = nullptr;
        cleanup();
        return false;
    }
    semaphore  = sem_open(sem_name.c_str(), 0);
    if(semaphore == SEM_FAILED){
        semaphore = nullptr;
        cleanup();
        return false;
    }
    return true;
}

bool SharedMemoryManager::lock(){
    if(semaphore == nullptr || semaphore == SEM_FAILED)
        return false;

    while(sem_wait(semaphore) == -1){
        if(errno == EINTR)
            continue;

        return false;
    }
    return true;
}

bool SharedMemoryManager::unlock(){
    if(semaphore == nullptr || semaphore == SEM_FAILED)
        return false;

    return sem_post(semaphore) == 0;
}

void* SharedMemoryManager::get_base_ptr(){
    return mapped_ptr;
}

void SharedMemoryManager::cleanup(){
    if(mapped_ptr && mapped_ptr != MAP_FAILED){
        munmap(mapped_ptr, shm_size);
        mapped_ptr = nullptr;
    }
    if(shm_fd != -1){
        close(shm_fd);
        shm_fd = -1;
    }
    if(semaphore && semaphore != SEM_FAILED){
        sem_close(semaphore);
        semaphore = nullptr;
    }
    if(is_owner){
        shm_unlink(shm_name.c_str());
        sem_unlink(sem_name.c_str());
    }
}
