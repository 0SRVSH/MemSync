# MemSync : POSIX Shared-Memory Key-Value Cache

A high-performance, local inter-process communication (IPC) key-value cache prototype built with C++ and POSIX shared memory.

When multiple processes on the same machine access a cache through sockets, each request may involve system calls, serialization, protocol handling, and additional data copying. For local processes, some of this overhead can be avoided. This project proposes a small proof of concept key value cache based on POSIX shared memory. 

## 🚀 Build and Run (Linux / WSL)

On Ubuntu or WSL, install CMake and a C++ compiler if needed:

```bash
sudo apt update
sudo apt install cmake build-essential
```

From the repository root, configure and build:

```bash
cmake -S . -B build
cmake --build build
```

To see the writer and reader share the cache, start the writer in one terminal:

```bash
./build/memsync_writer
```

Then, while the writer is running, start the reader in a second terminal:

```bash
./build/memsync_reader
```

The writer inserts sample entries, and the reader polls the shared cache. When both programs have finished, remove the shared-memory resource with:

```bash
./build/memsync_cleanup
```

The standalone cache demo can be run separately with `./build/memsync_test`. Do not run it at the same time as the writer or reader: it resets the shared-memory resource when it starts. The test and demo programs are currently manual demos, not an automated test suite.


---

## 🛠️ Tech Stack

* **Language:** C++
* **Environment:** Linux / WSL
* **Database Concepts:** Record layout, hashing, indexing, key-value storage, timestamps, concurrency control
* **Containerization / Build:** Docker, CMake, Git / GitHub
* **Testing & Benchmarking:** Manual demo; automated tests and benchmarks are planned

---

## 💡 Motivation

Modern applications frequently use in-memory caches to reduce database access latency and computational overhead. However, when multiple local processes on the same machine communicate via network sockets or loopback interfaces, each request incurs unnecessary overhead:
* System calls and context switches
* Serialization and deserialization
* Protocol handling and data copying

For local processes residing on the same host, this overhead can be largely avoided. This project proposes a proof-of-concept local key-value cache leveraging **POSIX shared memory**. 

Independent processes map the same underlying physical memory region using `shm_open()` and `mmap()`, allowing them to access shared cached records directly with minimal latency. Robust synchronization primitives protect concurrent updates across processes.

### Core Objectives
This project bridges **Operating Systems** concepts (shared memory, IPC, memory mapping, synchronization, process isolation, and resource cleanup) with **Database Management System (DBMS)** concepts (record layout, hashing, indexing, key-value storage, timestamps, and concurrency control). 

*Note: The primary objective is not to replace production systems like Redis or enterprise DBMS, but to build a focused prototype and rigorously evaluate shared-memory-based local caching.*

---

## 🎯 Project Goals

1. **Shared Memory Setup:** Create a POSIX shared memory segment accessible concurrently by multiple independent processes.
2. **Fixed-Size Storage & Indexing:** Implement fixed-size key-value slots coupled with a hash lookup mechanism.
3. **Operations Support:** Provide thread-safe and process-safe `PUT`, `GET`, and `UPDATE` operations.
4. **Pointer Independence:** Utilize relative offsets or array indices instead of process-specific absolute pointers within shared structures.
5. **Lifecycle Management:** Provide robust cleanup and restart handling for stale shared memory and semaphores.
6. **Performance Benchmarking:** Quantify latency and throughput improvements against local IPC baselines.

---

## 🏗️ Project Approach & Architecture

The prototype is implemented in C/C++ on Linux/WSL using native POSIX APIs. 

* **Initialization:** A cache-server process creates a named shared-memory object, allocates a fixed capacity (~4 MB), initializes metadata, and maps it using `mmap()`.
* **Client Attachment:** Client processes open the existing named object and map the identical physical memory into their respective address spaces.
* **Memory Layout:** The shared memory region consists of a global header followed by a fixed-capacity array of slots. Each slot stores:
  * Lifecycle/validity state
  * Fixed-size key and value buffers
  * Timestamps (creation/last-modified)
* **Collision Resolution:** A deterministic hash function selects the initial slot, and linear probing handles collisions. Fixed-size records intentionally avoid the memory fragmentation and complexity of a general-purpose dynamic allocator.
* **Concurrency Control:** Cache operations use a process-shared POSIX reader-writer lock.
* **Testing:** The current programs demonstrate basic cache operations and cleanup. Automated multi-client tests and performance benchmarks remain future work.

---

## 📋 Assumptions & Scope

* **Environment:** Runs exclusively on a single Linux/WSL machine with native POSIX shared-memory and semaphore kernel support.
* **Constraints:** Keys and values have strict fixed maximum sizes; cache total capacity is fixed at initialization.
* **Trust Model:** Clients are trusted local processes sharing identical structure definitions and header contracts.
* **Out of Scope:** Persistence across machine reboots, distributed/network clients, SQL support, replication, authentication, encryption, dynamic resizing, and production-grade fault tolerance.
* **Design Trade-off:** Cache operations currently use a process-shared reader-writer lock.

---


## 📚 References

1. **Linux man-pages:** `shm_open(3)`, `mmap(2)`, `ftruncate(2)`, `sem_open(3)`, `sem_wait(3)`, `sem_post(3)`.
2. Silberschatz, Galvin, and Gagne, *Operating System Concepts*.
3. Elmasri and Navathe, *Fundamentals of Database Systems*.
4. Redis Documentation — Background on in-memory key-value caching design.
5. POSIX / The Open Group Specifications for Shared Memory and Semaphore Interfaces.

---

## 🗂️ Project Repository Structure

```text
.
├── CMakeLists.txt             # CMake build configuration
├── README.md                  # Project overview and instructions
├── LICENSE
├── include/
│   ├── cache_engine.h         # Cache data structures and operations
│   ├── shared_memory.h        # Shared-memory manager interface
│   └── include.txt            # Folder note
├── src/
│   ├── cache_engine.cpp       # Cache implementation
│   ├── cleanup.cpp            # IPC resource cleanup program
│   ├── main.cpp               # Cache demo/test program
│   ├── reader.cpp             # Shared-cache reader example
│   ├── shared_memory.cpp      # Shared-memory manager implementation
│   ├── writer.cpp             # Shared-cache writer example
│   └── src.txt                # Folder note
├── tests/
│   └── tests.txt              # Folder note; automated tests are not yet added
├── docs/
│   └── docs.txt               # Folder note; project documentation is not yet added
├── memsync_reader            # Existing prebuilt executable
├── memsync_test              # Existing prebuilt executable
└── memsync_writer            # Existing prebuilt executable
