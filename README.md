# MemSync : POSIX Shared-Memory Key-Value Cache

A local inter-process communication (IPC) key-value cache prototype built with C++ and POSIX shared memory.

When multiple processes on the same machine access a cache through sockets, each request may involve system calls, serialization, protocol handling, and additional data copying. For local processes, some of this overhead can be avoided. This project proposes a small proof of concept key value cache based on POSIX shared memory. 

## 🚀 Build and Run (Linux / WSL)

On Ubuntu or WSL, install CMake, a C++ compiler, and GoogleTest:

```bash
sudo apt update
sudo apt install cmake build-essential libgtest-dev
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

The writer inserts sample entries, and the reader polls the shared cache. When both programs have finished and no process is attached, remove the shared-memory resource with:

```bash
./build/memsync_cleanup
```

The standalone cache demo can be run separately with `./build/memsync_test`. Do not run it at the same time as the writer, reader, or UI: it intentionally resets the shared-memory resource when it starts and removes it when it finishes.

## 🧪 Automated Tests

The automated cache tests use GoogleTest and are run through CTest. After installing the dependencies and building the project as described above, run:

```bash
ctest --test-dir build --output-on-failure
```

CTest reports each test separately. `--output-on-failure` shows details for tests that fail.

The CTest suite is split by purpose. Unit tests use a fresh in-memory cache and cover CRUD, input limits, collisions/tombstones, TTL, capacity/eviction, telemetry, and concurrent mutation invariants. Process integration tests use unique POSIX shared-memory names and cover concurrent initialization, cross-process value and TTL visibility, colliding mixed operations, full-capacity inserts, attach failures, persistence across reattachment, and unlink behavior. Child processes have bounded waits and tests clean up only the resources they created.

Run only the fast unit suite, only process integration tests, or both:

```bash
ctest --test-dir build -L unit --output-on-failure
ctest --test-dir build -L integration --output-on-failure
ctest --test-dir build --output-on-failure
```

The supported cache API requires non-empty keys of at most 63 bytes and values of at most 255 bytes. Empty values are valid. TTL `0` means no expiration; otherwise an entry expires when elapsed time is greater than or equal to its TTL. Updating a live key refreshes its timestamp and restarts the same TTL duration. At capacity, expired entries are reclaimed first; otherwise the oldest insertion/update timestamp is evicted, with the lowest slot index breaking timestamp ties.

### Opt-in performance benchmark

`memsync_benchmark` compares cache operations with a local Unix-domain `socketpair` request/response cache. It measures GET hits, GET misses, PUT/update, and mixed workloads using 16-, 128-, and 255-byte values at one and four client threads. Each row reports operation count, failures, median/p95/p99 per-operation latency, throughput, and host/compiler/build metadata. Setup, warm-up, and socket/thread startup are excluded from measured operation latencies. The benchmark is opt-in and does not affect CTest pass/fail. Its entry point, socket baseline, measurement/workload logic, and reporting helpers are organized in separate files under `bench/`.

```bash
mkdir -p bench/results
./build/memsync_benchmark --operations 2000 --trials 3 \
  > bench/results/benchmark.csv 2> bench/results/benchmark-summary.txt
```

The `bench/results/` directory is ignored by Git because benchmark reports vary by machine and run. Use the CSV and summary together with the command and environment when publishing results. The socketpair baseline and workloads are intended for local comparative measurements; do not infer a general performance advantage without repeated runs and interpretation of the results.

## 🖥️ Streamlit Evaluator Console

The optional Streamlit UI lets evaluators inspect the shared-memory cache, perform CRUD operations, run the existing CTest cases, and view a plain-language performance comparison without using a terminal. It runs in Linux/WSL, like the cache itself.

After installing the project prerequisites and building it, install the UI dependency and start the app from the repository root:

```bash
python3 -m pip install -r ui/requirements.txt
streamlit run ui/app.py
```

The CMake build creates `memsync_ui_bridge` and `memsync_cleanup`, which the app uses to call the existing C++ cache engine and cleanup utility. In **Cache Explorer**, evaluators can create keys (with optional TTL), read, update, and delete them, and view stored records and cache telemetry. TTL entries expire when elapsed time is greater than or equal to the TTL. Expired entries remain listed until a read lazily removes them or capacity pressure reclaims them. Updates fail for expired entries; a successful update refreshes the timestamp and restarts the existing TTL duration.

The sidebar's **Danger zone: destroy shared-memory cache** runs the same cleanup utility as `./build/memsync_cleanup`. To reduce accidental cleanup, it requires checking that all cache clients have stopped and typing `UNLINK`. POSIX shared memory does not provide a reliable way for the UI to confirm that no other process has an open mapping, so this is an operator-confirmed safety guard, not automatic detection. Do not initialize a new cache until all previous clients are stopped.

In **Test Lab**, the UI discovers cases through CTest. Evaluators can select and run individual cases or subsets, or run all discovered tests. Results include pass/fail status, per-test duration, aggregate timing, and failure output.

In **Benchmark**, evaluators can run the built `memsync_benchmark` executable and compare shared memory with the local Unix-socket baseline. The screen describes each workload in everyday terms, reports average latency and throughput across trials, and includes p95/p99 latency and failure counts for context. The benchmark runs all workload, payload-size, and thread-count combinations; larger operation counts and more trials take longer. Results are specific to the machine and run, and should not be presented as a universal performance guarantee.

The UI uses the same `/memsync_shm` resource as `memsync_writer` and `memsync_reader`. Its lifecycle is absent → exclusively created/unpublished → ready → attached; a partial or invalid object is never treated as ready. The process that wins `O_CREAT | O_EXCL` alone sizes and initializes the versioned header, slots, and process-shared reader-writer lock, then publishes the ready marker last. Attachers validate the exact object size and layout before accessing the lock and fail with a retryable error if initialization is still in progress. Existing valid caches are attached without reset. The bridge's **Initialize cache** action creates the cache if absent and leaves an already valid cache and its records unchanged.

The UI keeps `ui/app.py` as its Streamlit entry point. Cache, test, and benchmark screens live in separate view modules, shared bridge/CTest/benchmark calls are in `ui/services.py`, and project setup controls are in `ui/project_sidebar.py`.

The cache uses only the process-shared `pthread_rwlock_t` in its shared header for synchronization. `legacy/shared_memory.cpp` and `legacy/shared_memory.h` contain the unused semaphore-manager prototype and are not part of the supported build. Creators and clients close only their own mappings; creator exit does not unlink the named object. `memsync_cleanup` explicitly unlinks the name and is idempotent when it is absent. Unlinking does not invalidate mappings already held by clients, but a later creator can create a different object under the same name; run cleanup only after all clients have exited. A crash or initialization failure can leave an invalid object. It is not automatically reinitialized and its lock is never guessed to be safe to reset; after confirming no clients remain, use cleanup before restarting. The standalone `memsync_test` demo intentionally unlinks the named cache on startup and exit, so never run it alongside writer, reader, or UI clients.

Telemetry counts one hit or miss for every valid `GET` query. The collision counter counts each extra slot inspected during hash-table searches by cache operations; it excludes direct `find_slot` helper calls. Counter writes use the exclusive rwlock, while telemetry snapshots use the shared lock. Timestamps are Unix seconds for the most recent insertion or successful update—not creation time or last access time. A process that dies while holding the non-robust process-shared rwlock can leave the cache unavailable; automatic lock recovery is not supported.


---

## 🛠️ Tech Stack

* **Language:** C++
* **Environment:** Linux / WSL
* **Database Concepts:** Record layout, hashing, indexing, key-value storage, timestamps, concurrency control
* **Build:** CMake
* **Testing & Benchmarking:** GoogleTest/CTest unit and process integration tests; opt-in shared-memory vs. local Unix-domain socket benchmark

---

## 💡 Motivation

Modern applications frequently use in-memory caches to reduce database access latency and computational overhead. However, when multiple local processes on the same machine communicate via network sockets or loopback interfaces, each request incurs unnecessary overhead:
* System calls and context switches
* Serialization and deserialization
* Protocol handling and data copying

For local processes residing on the same host, this overhead can be largely avoided. This project proposes a proof-of-concept local key-value cache leveraging **POSIX shared memory**. 

Independent processes map the same underlying physical memory region using `shm_open()` and `mmap()`, allowing them to access shared cached records directly. A process-shared reader-writer lock coordinates access. Comparative performance is measured by an opt-in benchmark; no general speed advantage is assumed.

### Core Objectives
This project bridges **Operating Systems** concepts (shared memory, IPC, memory mapping, synchronization, process isolation, and resource cleanup) with **Database Management System (DBMS)** concepts (record layout, hashing, indexing, key-value storage, timestamps, and concurrency control). 

*Note: The primary objective is not to replace production systems like Redis or enterprise DBMS, but to build a focused prototype and rigorously evaluate shared-memory-based local caching.*

---

## 🎯 Project Goals

1. **Shared Memory Setup:** Create a POSIX shared memory segment accessible concurrently by multiple independent processes.
2. **Fixed-Size Storage & Indexing:** Implement fixed-size key-value slots coupled with a hash lookup mechanism.
3. **Operations Support:** Provide thread-safe and process-safe `PUT`, `GET`, and `UPDATE` operations.
4. **Pointer Independence:** Utilize relative offsets or array indices instead of process-specific absolute pointers within shared structures.
5. **Lifecycle Management:** Provide explicit cleanup and restart handling for the named shared-memory cache.
6. **Performance Benchmarking:** Measure latency and throughput against a local IPC baseline.

---

## 🏗️ Project Approach & Architecture

The prototype is implemented in C/C++ on Linux/WSL using native POSIX APIs. 

* **Initialization:** The process that successfully creates the named shared-memory object with exclusive creation allocates the fixed 4 MB region, initializes metadata, slots, and the process-shared rwlock, then publishes the ready marker last. Attachers validate the region and never initialize an existing object.
* **Client Attachment:** Client processes open the existing named object and map the identical physical memory into their respective address spaces.
* **Memory Layout:** A versioned shared-memory header is followed by a fixed-capacity array of slots. Each slot stores:
  * Lifecycle/validity state
  * Fixed-size key and value buffers
  * A Unix timestamp for the most recent insertion or successful update; reads do not refresh it
* **TTL and Capacity:** Entries expire when elapsed time is greater than or equal to the TTL. A successful update refreshes the timestamp while retaining the TTL duration. At capacity, expired entries are reclaimed first; otherwise the oldest insertion/update timestamp is evicted. Equal timestamps are resolved by the lowest occupied slot index.
* **Collision Resolution:** A deterministic hash function selects the initial slot, and linear probing handles collisions. Fixed-size records intentionally avoid the memory fragmentation and complexity of a general-purpose dynamic allocator.
* **Concurrency Control:** Cache operations use the process-shared POSIX reader-writer lock stored in the header. Lookup results are copied while holding a read lock; telemetry is updated afterward under an exclusive lock. Hits and misses count `GET` operations, and collisions count extra slots inspected by cache operation searches.
* **Resource Ownership:** The exclusive creator initializes the cache, all clients close their own mappings, and an operator runs cleanup only after clients exit. The cleanup utility does not detect active clients. The unused semaphore manager is legacy code and is excluded from the build.
* **Testing:** GoogleTest/CTest separates deterministic unit tests from bounded process integration tests. An opt-in benchmark reports per-operation latency distributions and throughput against a Unix-domain socketpair baseline. Comparative performance claims remain unverified until repeated benchmark runs are interpreted and published.

---

## 📋 Assumptions & Scope

* **Environment:** Runs on a single Linux/WSL machine with native POSIX shared-memory and process-shared reader-writer lock support.
* **Constraints:** Non-empty keys are limited to 63 bytes, values to 255 bytes (empty values are allowed), and cache capacity is fixed at initialization.
* **Trust Model:** Clients are trusted local processes sharing identical structure definitions and header contracts.
* **Out of Scope:** Persistence across machine reboots, distributed/network clients, SQL support, replication, authentication, encryption, dynamic resizing, and production-grade fault tolerance.
* **Design Trade-off:** Cache operations currently use one process-shared reader-writer lock; named semaphores are not part of the supported architecture.

---


## 📚 References

1. **Linux man-pages:** `shm_open(3)`, `mmap(2)`, `ftruncate(2)`, `pthread_rwlock_init(3)`.
2. Silberschatz, Galvin, and Gagne, *Operating System Concepts*.
3. Elmasri and Navathe, *Fundamentals of Database Systems*.
4. Redis Documentation — Background on in-memory key-value caching design.
5. POSIX / The Open Group Specifications for Shared Memory and Thread Synchronization.

---

## 🗂️ Project Repository Structure

Supported production headers belong in `include/`; the unbuilt semaphore prototype is kept under `legacy/`. The test fixture header is kept in `tests/` because it is only used by test code. CMake's generated `build/` directory, Python virtual environments, and machine-specific benchmark reports are local output and ignored by Git.

```text
.
├── CMakeLists.txt             # CMake build configuration and labeled test targets
├── .gitignore                 # Local build and environment exclusions
├── README.md                  # Project overview and instructions
├── LICENSE
├── include/
│   └── cache_engine.h         # Cache data structures and operations
├── legacy/
│   ├── shared_memory.h        # Unused semaphore-manager prototype interface
│   └── shared_memory.cpp      # Unused semaphore-manager prototype (not built)
├── src/
│   ├── cache_engine.cpp       # Cache implementation
│   ├── cleanup.cpp            # IPC resource cleanup program
│   ├── main.cpp               # Cache demo/test program
│   ├── reader.cpp             # Shared-cache reader example
│   ├── ui_bridge.cpp          # CLI bridge used by the Streamlit UI
│   └── writer.cpp             # Shared-cache writer example
├── tests/
│   ├── cache_test_fixture.h   # Shared setup and cleanup for unit tests
│   ├── cache_crud_test.cpp    # Cache operation tests
│   ├── cache_collision_test.cpp # Hash collision and tombstone tests
│   ├── cache_ttl_test.cpp     # TTL tests
│   ├── cache_capacity_test.cpp # Capacity and eviction tests
│   └── cache_shared_memory_test.cpp # Process integration tests
├── ui/
│   ├── app.py                  # Streamlit entry point and screen composition
│   ├── benchmark_view.py       # Benchmark runner and results view
│   ├── cache_view.py           # Cache explorer and CRUD interface
│   ├── project_sidebar.py      # Build directory and cache lifecycle controls
│   ├── services.py             # Cache bridge and CTest subprocess helpers
│   ├── test_view.py            # CTest test lab interface
│   └── requirements.txt        # UI Python dependencies
└── bench/
    ├── benchmark.h            # Benchmark data types and module interfaces
    ├── benchmark_measurement.cpp # Workloads, warm-up, and timed operations
    ├── benchmark_reporting.cpp # Latency statistics and host/CSV helpers
    ├── benchmark_socket.cpp   # Local socket-based cache baseline
    ├── cache_benchmark.cpp    # Benchmark CLI and execution entry point
    └── results/               # Generated, ignored local CSV and summary output
