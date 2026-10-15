# ArbiTrade

**A multi-agent, distributed arbitrage network simulator built in pure C++.**

ArbiTrade is a high-performance, multi-agent trading simulator designed to detect cross-exchange arbitrage opportunities in real-time. Instead of relying on heavy third-party dependencies (like Redis or gRPC), this system utilizes a custom TCP multiplexed lock server, a single-threaded event loop, and a packed binary wire protocol to achieve microsecond-level concurrency control.

---

### Key Features

1. Custom Distributed Mutex Engine: Bypasses the overhead of external databases by implementing a single-threaded Lock Server. It uses OS-level I/O multiplexing (select()) to resolve race conditions between competing trading agents atomically.
2. Zero-Serialization Wire Protocol: Eliminates JSON/Protobuf parsing overhead. Market data and lock requests are transmitted as #pragma pack(1) POD (Plain Old Data) structs, allowing direct memory-mapping (memcpy) straight off the network card.
3. Algorithmic Spread Detection: On every order book update, compares each pair of venues quoting the symbol and picks the crossed market with the highest estimated profit (spread × executable volume). The pairwise scan is O(n²) in the number of venues, which is negligible for a handful of exchanges.
4. Fair Broadcast: The replayer fans each tick out to agents sequentially over TCP, rotating which agent is sent to first on every tick so no agent gets a permanent head start in the lock race.
5. Sub-Millisecond Telemetry Engine: Features a built-in metric tracker that captures wire-to-execution latency, automatically calculating and reporting Min, Max, Average, P95, and P99 latencies in pure microseconds.

## Architecture Overview

```
 ┌────────────────────────────────────────────────────────────────────┐
 │                         ArbiTrade System                           │
 │                                                                    │
 │   ┌────────────┐  TickData (TCP)   ┌──────────┐                    │
 │   │            │ ─────────────────>│ Agent A  │──┐                 │
 │   │  Replayer  │  TickData (TCP)   ├──────────┤  │  LockRequest    │
 │   │ (CSV Feed) │ ─────────────────>│ Agent B  │──┤ ─────────────>  │
 │   │            │  TickData (TCP)   ├──────────┤  │  LockResponse   │
 │   │            │ ─────────────────>│ Agent C  │──┘ <─────────────  │
 │   └────────────┘                   └──────────┘                    │
 │                                                     ┌────────────┐ │
 │                                                     │ Lock Server│ │
 │                                                     │  (port     │ │
 │                                                     │   7777)    │ │
 │                                                     └────────────┘ │
 └────────────────────────────────────────────────────────────────────┘

```

---

## Prerequisites

| Requirement      | Version           | Notes                                             |
| ---------------- | ----------------- | ------------------------------------------------- |
| **OS**           | Windows 10 / 11   | Native Windows build — WSL is not supported       |
| **Compiler**     | MSVC (Visual C++) | Part of Visual Studio 2019 or 2022                |
| **Build Tool**   | CMake             | 3.20 or higher                                    |
| **C++ Standard** | C++20             | Set in CMakeLists.txt (the code itself uses C++17 features such as `std::optional`) |

> **Opening the right terminal matters.** All build commands below must be run from a **Developer Command Prompt for VS** (or **Developer PowerShell for VS**), not a plain `cmd` or PowerShell window. This is what puts `cl.exe`, `nmake.exe`, and the MSVC toolchain on your `PATH`.
>
> To open it: **Start Menu → search "Developer Command Prompt for VS 2022" → open**.

---

## Building the Project

### 1. Clone the project

```bash
git clone https://github.com/dishashetty06/arbitrade.git
cd arbitrade

```

### 2. Create the build directory and configure

```bash
mkdir build
cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
```

You should see output ending with:

```
-- ArbiTrade configured: CXX=20, BuildType=Release
-- Build files have been written to: C:/arbitrade/build
```

If CMake cannot find the compiler, confirm you opened a Developer Command Prompt (not a plain terminal).

### 3. Compile

```bash
cmake --build . --config Release
```

---

## Running the System

Start four processes (the lock server, two agents, and the replayer) **in this order**. Each agent connects to the lock server on startup (retrying for about 5 seconds), then blocks until the replayer connects to it. The replayer needs every agent to already be listening.

Open **four separate terminal windows**, all from the **project root** (the folder you cloned into), not from inside `build\`.

---

### Terminal 1 — Lock Server

```bash
build\bin\lock_server.exe
```

Expected output:

```
[12:00:00.001] [INFO ] LockServer: ArbiTrade Distributed Lock Server starting...
[12:00:00.002] [INFO ] LockServer: listening on port 7777
```

The lock server is now ready and will wait for agents to connect.

---

### Terminal 2 — Agent One (port 9997)

```bash
build\bin\agent.exe 9997 Alpha
```

Expected output:

```
[12:00:01.010] [INFO ] Agent: 'Alpha' starting...
[12:00:01.015] [INFO ] TcpClient: connecting to 127.0.0.1:7777 ...
[12:00:01.018] [INFO ] TcpClient: connected successfully.
[12:00:01.019] [INFO ] Agent: 'Alpha' established persistent connection to lock server.
[12:00:01.020] [INFO ] TcpServer: listening on port 9997
[12:00:01.021] [INFO ] Agent: 'Alpha' waiting for replayer connection on port 9997...
```

---

### Terminal 3 — Agent Two (port 9998)

```bash
build\bin\agent.exe 9998 Beta
```

Expected output (same pattern, different port and name):

```
[12:00:02.010] [INFO ] Agent: 'Beta' starting...
[12:00:02.015] [INFO ] TcpClient: connecting to 127.0.0.1:7777 ...
[12:00:02.018] [INFO ] TcpClient: connected successfully.
[12:00:02.019] [INFO ] Agent: 'Beta' established persistent connection to lock server.
[12:00:02.020] [INFO ] TcpServer: listening on port 9998
[12:00:02.021] [INFO ] Agent: 'Beta' waiting for replayer connection on port 9998...
```

---

### Terminal 4 — Replayer (broadcasts to both agents)

```bash
build\bin\replayer.exe data\market_data.csv 9997 9998
```

Expected output:

```
[12:00:05.001] [INFO ] Replayer: ArbiTrade Market Data Replayer starting, targeting 2 agent(s)...
[12:00:05.010] [INFO ] TcpClient: connecting to 127.0.0.1:9997 ...
[12:00:05.012] [INFO ] TcpClient: connected successfully.
[12:00:05.013] [INFO ] Replayer: connected to agent on port 9997
[12:00:05.014] [INFO ] TcpClient: connecting to 127.0.0.1:9998 ...
[12:00:05.016] [INFO ] TcpClient: connected successfully.
[12:00:05.017] [INFO ] Replayer: connected to agent on port 9998
[12:00:05.018] [INFO ] Replayer: broadcast tick #1 to 2 agent(s)
...
```
