# MemSync : POSIX Shared-Memory Key-Value Cache

A high-performance, local inter-process communication (IPC) key-value cache prototype built with C++ and POSIX shared memory.

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

The current suite contains 24 tests:

* `PutThenGetReturnsStoredValue`: a stored key returns its value and a nonzero timestamp.
* `GetMissingKeyReturnsFalse`: looking up an absent key reports a miss.
* `PuttingSameKeyReplacesValue`: putting an existing key replaces its value without increasing the entry count.
* `UpdateChangesValue`: updating an existing key changes the value returned by a later lookup.
* `UpdateMissingKeyReturnsFalse`: updating an absent key reports failure.
* `DeleteRemovesKey`: deleting a stored key makes it unavailable and decrements the entry count.
* `DeleteMissingKeyReturnsFalse`: deleting an absent key reports failure.
* `CollidingKeysCanBothBeRetrieved`: two keys with the same initial hash can both be retrieved.
* `ReusesDeletedSlotWithoutHidingCollidingKey`: a deleted collision slot can be reused while another colliding key remains findable.
* `TtlValueCanBeReadBeforeExpiration`: a value with a TTL can be read before it expires.
* `ExpiredTtlValueIsRemovedOnGet`: looking up an expired value reports a miss and removes it from the active count.
* `ConcurrentGetsCountEveryHitAndMissExactlyOnce`: concurrent successful and unsuccessful lookups produce exact hit/miss totals.
* `InitializationPublishesAValidVersionedLayout`: cache initialization creates a valid versioned layout that passes attachment validation.
* `ConcurrentProcessesPublishOneUsableCache`: concurrent processes publish exactly one initialized cache and all attachers can safely store records.
* `AttachingToPartialCacheFailsBeforeLockUse`: an existing but unpublished cache is rejected without touching its uninitialized lock.
* `ReopeningValidCachePreservesExistingRecords`: opening an already valid cache does not reinitialize or clear its records.
* `ConcurrentCollisionLookupsUpdateTelemetrySafely`: concurrent lookups of colliding keys produce exact collision telemetry.
* `ExpirationBoundaryIsInclusive`: a TTL entry expires when elapsed time is greater than or equal to its TTL.
* `ConcurrentExpiredGetsCountEachMissAndRemoveOnce`: concurrent expiration races count each miss and remove the record only once.
* `UpdateRejectsExpiredEntryWithoutHidingCollisions`: update cannot revive an expired entry or hide a later colliding key.
* `SuccessfulUpdateRefreshesTimestampAndRetainsTtl`: update refreshes the timestamp while retaining the TTL duration.
* `InsertingPastCapacityEvictsOldestWriteEntry`: inserting into a full cache evicts the entry with the oldest insertion/update timestamp.
* `ExpiredEntryIsReclaimedBeforeEvictingLiveEntry`: insertion reclaims an expired entry before evicting any live entry.
* `EqualTimestampsEvictTheLowestOccupiedSlot`: equal eviction timestamps are resolved deterministically by slot index.

The tests are grouped into `cache_crud_test.cpp`, `cache_collision_test.cpp`, `cache_ttl_test.cpp`, `cache_capacity_test.cpp`, and `cache_shared_memory_test.cpp`.

Cache-operation tests use a fresh in-memory cache via `cache_test_fixture.h`; shared-memory integration tests use unique temporary POSIX object names and clean them up. The suite covers thread-concurrent telemetry and multi-process startup, but not performance benchmarks.

## 🖥️ Streamlit Evaluator Console

The optional Streamlit UI lets evaluators inspect the shared-memory cache, perform CRUD operations, and run the existing CTest cases without using a terminal. It runs in Linux/WSL, like the cache itself.

After installing the project prerequisites and building it, install the UI dependency and start the app from the repository root:

```bash
python3 -m pip install -r ui/requirements.txt
streamlit run ui/app.py
```

The CMake build creates `memsync_ui_bridge`, which the app uses to call the existing C++ cache engine. In **Cache Explorer**, evaluators can create keys (with optional TTL), read, update, and delete them, and view stored records and cache telemetry. TTL entries expire when elapsed time is greater than or equal to the TTL. Expired entries remain listed until a read lazily removes them or capacity pressure reclaims them. Updates fail for expired entries; a successful update refreshes the timestamp and restarts the existing TTL duration.

In **Test Lab**, the UI discovers cases through CTest. Evaluators can select and run individual cases or subsets, or run all discovered tests. Results include pass/fail status, per-test duration, aggregate timing, and failure output.

The UI uses the same `/memsync_shm` resource as `memsync_writer` and `memsync_reader`. The process that creates the object initializes its versioned header, entries, and process-shared reader-writer lock before publishing the ready marker. Attachments validate the object size and layout before accessing the lock; an attachment that races initialization receives a retryable error. The bridge's **Initialize cache** action creates the cache if absent and leaves an already valid cache and its records unchanged.

The cache uses only the process-shared `pthread_rwlock_t` in its shared header for synchronization. `src/shared_memory.cpp` and `include/shared_memory.h` are unused legacy semaphore-manager code and are not part of the supported build. Cache creators initialize but do not unlink the named object on exit; each process closes its own mapping, and `memsync_cleanup` is the explicit unlink operation. Run cleanup only after all cache clients have exited: unlinking while clients remain attached can split them from processes that later create a new object. The shared layout is versioned, so incompatible or partially initialized objects must be removed with cleanup before reinitializing.

Telemetry counts one hit or miss for every valid `GET` query. The collision counter counts each extra slot inspected during hash-table searches by cache operations; it excludes direct `find_slot` helper calls. Counter writes use the exclusive rwlock, while telemetry snapshots use the shared lock.


---

## 🛠️ Tech Stack

* **Language:** C++
* **Environment:** Linux / WSL
* **Database Concepts:** Record layout, hashing, indexing, key-value storage, timestamps, concurrency control
* **Build:** CMake
* **Testing & Benchmarking:** GoogleTest and CTest for cache unit tests; performance benchmarks are planned

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
5. **Lifecycle Management:** Provide explicit cleanup and restart handling for the named shared-memory cache.
6. **Performance Benchmarking:** Quantify latency and throughput improvements against local IPC baselines.

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
* **Resource Ownership:** The exclusive creator initializes the cache, all clients close their own mappings, and the cleanup utility unlinks the name only when no clients remain active. The unused semaphore manager is legacy code and is excluded from the build.
* **Testing:** GoogleTest unit tests cover operations, thread-concurrent telemetry, collisions, TTL, initialization layout, and capacity policy. Multi-process integration tests and performance benchmarks remain future work.

---

## 📋 Assumptions & Scope

* **Environment:** Runs on a single Linux/WSL machine with native POSIX shared-memory and process-shared reader-writer lock support.
* **Constraints:** Keys and values have strict fixed maximum sizes; cache total capacity is fixed at initialization.
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

Production headers belong in `include/`. The test fixture header is kept in `tests/` because it is only used by test code. CMake's generated `build/` directory is local build output and is ignored by Git.

```text
.
├── CMakeLists.txt             # CMake build configuration
├── .gitignore                 # Local build and environment exclusions
├── README.md                  # Project overview and instructions
├── LICENSE
├── include/
│   ├── cache_engine.h         # Cache data structures and operations
│   └── shared_memory.h        # Shared-memory manager interface
├── src/
│   ├── cache_engine.cpp       # Cache implementation
│   ├── cleanup.cpp            # IPC resource cleanup program
│   ├── main.cpp               # Cache demo/test program
│   ├── reader.cpp             # Shared-cache reader example
│   ├── shared_memory.cpp      # Unused legacy semaphore manager (not built)
│   └── writer.cpp             # Shared-cache writer example
├── tests/
│   ├── cache_test_fixture.h   # Shared setup and cleanup for unit tests
│   ├── cache_crud_test.cpp    # Cache operation tests
│   ├── cache_collision_test.cpp # Hash collision and tombstone tests
│   ├── cache_ttl_test.cpp     # TTL tests
│   └── cache_capacity_test.cpp # Capacity and eviction test
