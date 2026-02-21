# jaldis-cpp

*Didn't spend a penny on EngineerPro's course (again). Worshipped the almighty footgun 🔫 instead.*

**jaldis** is a high-performance, Redis-compatible key-value store reimplemented in modern C++26. It is a spiritual successor to my own original [OCaml implementation](https://github.com/jalsol/jaldis), designed to explore lower-level performance characteristics and manual memory management strategies that were abstracted away in the functional version.

While the OCaml version focused on expressiveness and type safety, this C++ port leverages Linux-native asynchronous I/O (`epoll`) and custom memory allocators (`std::pmr`) to provide predictable latency and reduced runtime overhead.

## Benchmarking

Performance benchmarks using `redis-benchmark` against jaldis server:

### Test Configuration
- **Hardware**: Intel Core i5-1135G7
- **OS**: Linux 6.18.7-arch1-1
- **OCaml**: 5.3.0
- **Network**: localhost
- **Test Parameters**: 1,000,000 `SET/GET` operations, 100 concurrent connections

```bash
# Standard benchmark
redis-benchmark -h 127.0.0.1 -n 1000000 -c 100 -t set,get --csv

# Pipelined benchmark (16 commands per pipeline)
redis-benchmark -h 127.0.0.1 -n 1000000 -c 100 -t set,get -P 16 --csv
```

### Results (on Release build)

#### Standard Mode (`-P 1`)
```
"test","rps","avg_latency_ms","min_latency_ms","p50_latency_ms","p95_latency_ms","p99_latency_ms","max_latency_ms"
"SET","191570.89","0.265","0.064","0.263","0.303","0.375","1.583"
"GET","188040.62","0.270","0.080","0.271","0.311","0.367","1.343"
```

#### Pipelined Mode (`-P 16`)
```
"test","rps","avg_latency_ms","min_latency_ms","p50_latency_ms","p95_latency_ms","p99_latency_ms","max_latency_ms"
"SET","2024291.50","0.784","0.024","0.783","0.847","0.911","2.095"
"GET","2096436.12","0.753","0.016","0.751","0.799","1.415","2.367"
```

## Features

### Core Architecture
- **Language**: C++26
- **I/O Model**: Single-threaded, non-blocking event loop using `epoll` (Linux only).
- **Memory Management**: Uses `std::pmr::monotonic_buffer_resource` for zero-allocation parsing and request handling per client.
- **Protocol**: Full support for RESP (Redis Serialization Protocol).

For a deep dive into the system design, see [ARCHITECTURE.md](docs/ARCHITECTURE.md).

### Supported Data Types
- **Strings** - Basic key-value pairs.
- **Lists** - Double-ended queues (implemented via `std::deque`).
- **Sets** - Unordered collections of unique strings.

### Implemented Commands

#### Basic Operations
- `PING` - Test connection liveness.
- `SET` / `GET` - Store and retrieve string values.
- `DEL` - Remove keys.
- `KEYS` - List all keys (supports patterns, currently returns all keys).
- `FLUSHDB` - Remove all keys from the current database.

#### List Operations
- `LPUSH` / `RPUSH` - Push elements to the head/tail of a list.
- `LPOP` / `RPOP` - Pop elements from the head/tail.
- `LLEN` - Get the length of a list.
- `LRANGE` - Retrieve a range of elements from a list.

#### Set Operations
- `SADD` - Add members to a set.
- `SREM` - Remove members from a set.
- `SCARD` - Get the number of members in a set.
- `SMEMBERS` - Get all members of a set.
- `SINTER` - Intersect multiple sets.
- `SISMEMBER` - Check if a value is a member of a set.

#### Expiration
- `EXPIRE` - Set a timeout on a key (in seconds).
- `TTL` - Get the remaining time-to-live for a key.
- **Strategy**: Hybrid approach using lazy expiration on access and probabilistic active sweeping.

## Build Instructions

### Prerequisites
- A C++26 compliant compiler (e.g., GCC 14+, Clang 18+).
- CMake 3.25 or newer.
- Linux operating system (required for `epoll` support).

### Building from Source

```bash
# Clone the repository
git clone https://github.com/jalsol/jaldis.git
cd jaldis

# Configure the build
cmake -Bbuild -S.
cmake -Bbuild -S. -DCMAKE_BUILD_TYPE=Release # Release build

# Build the project
cmake --build build -j
```

The build process will automatically fetch dependencies (Catch2 for testing) using CMake's `FetchContent`.

## Usage

Start the server:

```bash
./build/jaldis
```

By default, the server binds to `127.0.0.1` on port `6379`. You can connect using any standard Redis client:

```bash
redis-cli -p 6379 PING
# PONG
```

## Running Tests

The project includes a comprehensive test suite covering the RESP parser, command logic, and storage engine.

```bash
# Run all tests via CTest
cd build
ctest --output-on-failure

# Or run individual test binaries directly
./build/resp_tests     # Protocol parser/serializer tests
./build/storage_tests  # Core storage engine tests
./build/command_tests  # Command logic tests
```

## License

MIT License - see [LICENSE](LICENSE) file for details.
