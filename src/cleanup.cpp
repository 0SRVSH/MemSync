#include <iostream>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
using namespace std;
#define SHM_NAME "/memsync_shm"
#define SEM_NAME "/memsync_sem"

int main() {
    cout << "[OS Cleanup] Checking for stale Linux IPC resources...\n";

    // 1. Unlink the shared memory segment
    if (shm_unlink(SHM_NAME) == 0) {
        cout << "[SUCCESS] Unlinked shared memory: " << SHM_NAME << "\n";
    } else {
        cout << "[INFO] No active shared memory found for: " << SHM_NAME << "\n";
    }

    // 2. Unlink the semaphore
    if (sem_unlink(SEM_NAME) == 0) {
        cout << "[SUCCESS] Unlinked semaphore: " << SEM_NAME << "\n";
    } else {
        cout << "[INFO] No active semaphore found for: " << SEM_NAME << "\n";
    }

    cout << "[OS Cleanup] Cleanup complete.\n";
    return 0;
}