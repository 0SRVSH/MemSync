# MemSync : POSIX Shared-Memory Key-Value Cache

A high-performance, local inter-process communication (IPC) key-value cache prototype built with C++ and POSIX shared memory.

When multiple processes on the same machine access a cache through sockets, each request may involve system calls, serialization, protocol handling, and additional data copying. For local processes, some of this overhead can be avoided. This project proposes a small proof of concept key value cache based on POSIX shared memory. 

---

## 🛠️ Tech Stack

* **Language:** C++
* **Environment:** Linux / WSL
* **Database Concepts:** Record layout, hashing, indexing, key-value storage, timestamps, concurrency control
* **Containerization / Build:** Docker, CMake, Git / GitHub
* **Testing & Benchmarking:** GoogleTest, Google Benchmark

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
* **Concurrency Control:** A named POSIX semaphore (`sem_open`, `sem_wait`, `sem_post`) coordinates modifications to guarantee atomicity.
* **Testing:** Comprehensive test suites cover multi-client access, hash collisions, repeated updates, capacity saturation, crash/cleanup recovery scenarios, and micro-benchmarks.

---

## 🗺️ 4-Month Implementation Roadmap

* **Month 1 — IPC Foundation:** 
  * Initialize shared memory using `shm_open()`, `ftruncate()`, and `mmap()`.
  * Establish basic inter-process communication by exchanging string buffers between two independent processes.
* **Month 2 — Cache Engine:** 
  * Design and implement slot structures, hash functions, and collision resolution (linear probing).
  * Implement core operations (`PUT`, `GET`, `UPDATE`) and metadata/timestamps.
* **Month 3 — Concurrency & Robustness:** 
  * Integrate POSIX semaphore locking mechanisms for safe multi-client access.
  * Develop multi-client integration tests, error checking, and cleanup/restart behavior handling.
* **Month 4 — Evaluation & Demonstration:** 
  * Run latency and throughput benchmarks.
  * Compare performance against a simple IPC baseline.
  * Document architectural limitations and prepare the final demonstration.

---

## 📋 Assumptions & Scope

* **Environment:** Runs exclusively on a single Linux/WSL machine with native POSIX shared-memory and semaphore kernel support.
* **Constraints:** Keys and values have strict fixed maximum sizes; cache total capacity is fixed at initialization.
* **Trust Model:** Clients are trusted local processes sharing identical structure definitions and header contracts.
* **Out of Scope:** Persistence across machine reboots, distributed/network clients, SQL support, replication, authentication, encryption, dynamic resizing, and production-grade fault tolerance.
* **Design Trade-off:** A coarse-grained lock is prioritized initially to guarantee correctness and feasibility within the 4-month timeline.

---


## 📚 References

1. **Linux man-pages:** `shm_open(3)`, `mmap(2)`, `ftruncate(2)`, `sem_open(3)`, `sem_wait(3)`, `sem_post(3)`.
2. Silberschatz, Galvin, and Gagne, *Operating System Concepts*.
3. Elmasri and Navathe, *Fundamentals of Database Systems*.
4. Redis Documentation — Background on in-memory key-value caching design.
5. POSIX / The Open Group Specifications for Shared Memory and Semaphore Interfaces.

---

## 🗂️ Proposed Project Repository Structure

```text
├── include/          # Header files containing public interfaces, structs, and relative-offset memory layouts
├── src/              # Implementation source files for the OS memory wrapper, database engine, server daemon, and client API
├── tests/            # Multi-process concurrency validation scripts and automated stress testers
├── docs/             # Architecture diagrams and design notes for mentor reviews
├── CMakeLists.txt    # Unified build automation script so everyone compiles identically
└── README.md         # Project overview, architecture breakdown, and compilation steps

