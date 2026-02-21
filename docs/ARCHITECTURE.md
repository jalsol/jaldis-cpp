# Architecture of jaldis-cpp

This document provides a technical overview of the `jaldis-cpp` architecture. The project is designed as a single-threaded, event-driven server optimized for low latency and minimal allocation overhead.

## Design Philosophy

The primary goal of `jaldis-cpp` is to replicate the functionality of the original OCaml `jaldis` while taking explicit control over system resources. Where the OCaml version relied on the garbage collector and high-level async libraries, this C++ implementation manages memory manually and interacts directly with the OS kernel.

Key differences from the OCaml version:
- **Zero-Allocation Parsing**: Using `std::pmr` to avoid heap allocations during request processing.
- **Direct Syscalls**: Using `epoll` directly instead of an abstraction layer.
- **Cache Locality**: Optimizing data structures for CPU cache performance.

## Core Components

### 1. The Event Loop (`server.cpp`)

The heart of the server is a custom event loop built on Linux's `epoll` mechanism.

- **Single-Threaded**: All processing happens in a single thread to avoid context switching and synchronization overhead (mutexes/locks). This mimics the architecture of Redis itself.
- **Non-Blocking I/O**: All socket operations are non-blocking. The server only reads when data is available and writes when the socket is ready.
- **State Machine**: Each client connection maintains its own state (parsing progress, buffers), allowing the server to handle thousands of concurrent connections efficiently.

### 2. Memory Management (`std::pmr`)

One of the most significant performance features is the use of C++17's Polymorphic Memory Resources (`std::pmr`).

- **Per-Client Arenas**: Each client connection is assigned a fixed-size `std::byte` buffer (stack or heap-allocated at connection start).
- **Monotonic Buffers**: A `std::pmr::monotonic_buffer_resource` wraps this buffer.
- **Allocation Strategy**:
    - When a request arrives, the RESP parser allocates nodes (strings, arrays) from this monotonic buffer.
    - This allocation is effectively a pointer bump, which is extremely fast (nanoseconds).
    - **Reset**: After the command is executed and the response is sent, the entire buffer is "released" by simply resetting the monotonic resource's head pointer. No individual `free()` calls are made.

### 3. The Storage Engine (`storage.cpp`)

The storage layer is a wrapper around standard C++ containers, but with a unified interface.

- **Variant Value Type**: Values are stored as `std::variant<Storage::String, Storage::List, Storage::Set>`. This allows heterogenous data types to be stored in a single `std::unordered_map`.
- **Transparent Hashing**: The map uses `std::hash<std::string_view>` (transparent hashing) to allow lookups using `std::string_view` without allocating a temporary `std::string`.
- **Expiration Strategy**:
    - **Lazy Expiration**: Checks if a key is expired *before* accessing it. If it is, the key is deleted immediately.
    - **Active Sweeping**: A probabilistic algorithm runs periodically to sample keys and remove expired ones, preventing memory leaks from unused keys.

### 4. Command Dispatch (`command_handler.hpp`)

Commands are defined in a static registry pattern.

- **Compile-Time Registry**: The command table is built at compile time using C++20 `consteval`/`constexpr` features where possible.
- **Handler Signature**: All command handlers share a uniform signature:
  ```cpp
  resp::Type Handler(CommandArgs args, Storage& store, std::pmr::memory_resource* arena);
  ```
- **Execution Flow**:
    1. Parser produces a `resp::Type` (usually an Array of BulkStrings).
    2. The first element is treated as the command name.
    3. The dispatcher looks up the function pointer.
    4. The function is executed, modifying the `Storage` and returning a `resp::Type` result.
    5. The result is serialized back to the client.

### 5. RESP Protocol Handling (`resp/`)

- **Parser**: A hand-written recursive descent parser that constructs a `resp::Type` tree.
- **Serializer**: Converts `resp::Type` objects back into the wire format string.
- **Zero-Copy Intent**: The parser relies heavily on `std::string_view` where possible to avoid copying data from the read buffer until necessary (e.g., when storing a value).

## Future Improvements

- **Snapshotting (RDB)**: Implementing persistence to save the in-memory state to disk.
