# ArbiTrade ⚡

**A Low-Latency, Distributed Arbitrage Trading Network built in pure C++20.**

ArbiTrade is a high-performance, multi-agent trading simulator designed to detect cross-exchange arbitrage opportunities in real-time. Instead of relying on heavy third-party dependencies (like Redis or gRPC), this system utilizes a **custom TCP lock server**, a **zero-allocation hot path**, and a **packed binary wire protocol** to achieve microsecond-level concurrency control.

## 🧠 Core Architecture & Technical Highlights

* **Custom Distributed Mutex Engine:** Bypasses the overhead of external databases (e.g., Redis) by implementing a bespoke, single-threaded Lock Server. It uses OS-level I/O multiplexing (`select()`) to resolve race conditions between competing trading agents atomically.
* **Zero-Serialization Wire Protocol:** Eliminates JSON/Protobuf parsing overhead. Market data and lock requests are transmitted as `#pragma pack(1)` POD (Plain Old Data) C++ structs, allowing direct memory-mapping (`memcpy`) off the network stack.
* **Decoupled Event Pipeline:** Strictly adheres to Separation of Concerns. The network layer (Winsock2), state management (`OrderBook`), and decision logic (`SpreadDetector`) are entirely isolated.
