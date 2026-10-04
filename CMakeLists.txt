cmake_minimum_required(VERSION 3.16)

project(MemSync LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(
    memsync_cache
    src/cache_engine.cpp
)

target_include_directories(memsync_cache PUBLIC include)

add_executable(
    memsync_writer
    src/writer.cpp
)
target_link_libraries(memsync_writer PRIVATE memsync_cache)

add_executable(
    memsync_reader
    src/reader.cpp
)
target_link_libraries(memsync_reader PRIVATE memsync_cache)


add_executable(
    memsync_test
    src/main.cpp
)
target_link_libraries(memsync_test PRIVATE memsync_cache)


add_executable(
    memsync_cleanup
    src/cleanup.cpp
)