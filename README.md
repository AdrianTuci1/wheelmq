# 🎡 wheelmq

> **High-Performance P2P Distributed Timing & Queue Engine written in pure C11.**  
> Zero external dependencies · ~36 KB static binary · $O(1)$ Hierarchical Timing Wheel · Consistent Hash Ring Sharding · Dead Letter Queue (DLQ)

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](#)
[![Language](https://img.shields.io/badge/language-C11-blue.svg)](#)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](#)
[![Zero-Dependencies](https://img.shields.io/badge/dependencies-zero-orange.svg)](#)

---

## 📑 Table of Contents
1. [What is wheelmq?](#-what-is-wheelmq)
2. [Download & Installation](#-download--installation)
3. [Quickstart Guide](#-quickstart-guide)
4. [Dead Letter Queue (DLQ) & Failure Recovery](#-dead-letter-queue-dlq--failure-recovery)
5. [Cluster Pairing & P2P Sharding](#-cluster-pairing--p2p-sharding)
6. [Handling Large Messages](#-handling-large-messages)
7. [Full Command & Protocol Reference](#-full-command--protocol-reference)
8. [Performance Benchmarks](#-performance-benchmarks)

---

## 💡 What is wheelmq?

In production, traditional message brokers (Kafka, RabbitMQ) struggle with **delayed / scheduled tasks** at high throughput, while legacy cron daemons are notorious single points of failure (SPOF). To achieve high availability, DevOps teams are often forced to operate heavy distributed clusters (ZooKeeper, etcd, Redis, or PostgreSQL).

**`wheelmq`** bundles the four essential pillars of resilient backend infrastructure into a **single, ~36 KB static C binary**:

1. **Strict FIFO Message Queue** — Reliable, in-order task distribution to workers.
2. **Hierarchical Timing Wheel ($O(1)$)** — Tracks and cascades millions of future tasks (due in milliseconds, minutes, or days) with zero database scanning overhead.
3. **Dead Letter Queue (DLQ) & Auto-Retry** — When downstream workers fail, tasks are preserved with contextual error logs and can be inspected and replayed on demand.
4. **P2P Sharding via Consistent Hash Ring** — 1 node owns 100% of the ring; multiple nodes dynamically partition queues with zero master bottleneck.

```
                                PRODUCER
                                   │
              PUSH queue [delay_ms] payload
                                   │
                                   ▼
        ┌────────────────────────────────────────────────────────┐
        │                 CONSISTENT HASH RING                   │
        │           (MurmurHash3 + 128 Virtual Nodes)            │
        └──────────────────────────┬─────────────────────────────┘
                                   │
                     Routes to Owner Node in Cluster
                                   │
                     ┌─────────────┴─────────────┐
                     │ Has Scheduled Delay?      │
                    YES                          NO
                     │                           │
         ┌───────────▼────────────┐    ┌─────────▼────────────┐
         │ HIERARCHICAL WHEEL     │    │   ACTIVE FIFO QUEUE  │
         │  Level 0: 256 x 1ms    │    │      (O(1) Pop)      │
         │  Level 1: 64 x 256ms   │    └─────────▲────────────┘
         │  Level 2: 64 x 16.3s   │              │
         │  Level 3: 64 x 17.4m   │  Timer fires │
         │  Level 4: 64 x 18.6h   ├──────────────┘
         └────────────────────────┘
                                   │
                                WORKER
                                   │
                     ┌─────────────┴─────────────┐
                  SUCCESS                     FAILURE
                     │                           │
               [Ack & Done]                Retries < Max?
                                           ┌─────┴─────┐
                                          YES          NO
                                           │           │
                                     [Re-queue]   ┌────▼────┐
                                     (Backoff)    │   DLQ   │
                                                  └─────────┘
```

---

## 📦 Download & Installation

### Prerequisites
* Operating System: **Linux** or **macOS**
* C Compiler: **`gcc`** or **`clang`** (C11 support)
* Build Tool: **`make`** (No external libraries or runtimes required!)

### Build from Source
```bash
# 1. Clone the repository
git clone https://github.com/AdrianTuci1/wheelmq.git
cd wheelmq

# 2. Build the server daemon and CLI client
make all

# 3. Run the complete automated test suite
make test
```

Binaries will be placed in `bin/`:
* `bin/wheelmq` — The high-performance server daemon (~36 KB).
* `bin/wheelmq-cli` — The standalone command-line client utility (~33 KB).

---

## 🚀 Quickstart Guide

### Step 1: Start a `wheelmq` Node
Run the daemon on TCP port `7000`:
```bash
./bin/wheelmq --id node-1 --port 7000
```
> *Optionally bind to a UNIX domain socket for ultra-fast local IPC:*  
> `./bin/wheelmq --id node-1 --socket /tmp/wheelmq.sock`

### Step 2: Push an Immediate Message (FIFO)
```bash
./bin/wheelmq-cli -p 7000 push orders 0 "{\"order_id\": 1001, \"amount\": 49.99}"
```
*Server Output:* `OK id=1 status=ready size=40`

### Step 3: Push a Scheduled / Delayed Message
Schedule a message to become ready in **5 seconds** (5000 ms):
```bash
./bin/wheelmq-cli -p 7000 push orders 5000 "{\"order_id\": 1002, \"reminder\": true}"
```
*Server Output:* `OK id=2 status=scheduled delay_ms=5000 size=40`  
*(The message enters the Timing Wheel and is automatically cascaded into the ready FIFO queue after 5000 ms).*

### Step 4: Consume Messages from Queue (`POP`)
```bash
./bin/wheelmq-cli -p 7000 pop orders
```
*Response:*  
`MESSAGE id=1 retry=0 size=40 payload={"order_id": 1001, "amount": 49.99}`

If the queue is empty, the server returns `EMPTY`.

---

## 🛡️ Dead Letter Queue (DLQ) & Failure Recovery

When worker execution fails (e.g. database timeout or third-party API outage), jobs are not dropped. They are moved into the **Dead Letter Queue (DLQ)** along with the root cause for inspection and recovery.

### 1. Mark a Message as Failed (`FAIL`)
```bash
./bin/wheelmq-cli -p 7000 fail orders 101 "Stripe payment gateway 504 timeout"
```
*Response:* `OK id=101 moved_to_dlq reason="Stripe payment gateway 504 timeout"`

### 2. Inspect the Dead Letter Queue (`DLQ_LIST`)
```bash
./bin/wheelmq-cli -p 7000 dlq_list orders
```
*Sample Output:*
```text
DLQ_COUNT 2
  - id=101 retries=0 reason="Stripe payment gateway 504 timeout"
  - id=104 retries=3 reason="Downstream service unavailable"
```

### 3. Replay a Failed Message (`DLQ_REPLAY`)
Once downstream connectivity is restored, replay the failed job back into the active FIFO queue:
```bash
./bin/wheelmq-cli -p 7000 dlq_replay orders 101
```
*Response:* `OK replayed id=101`  
*(Job 101 is now immediately available in the active FIFO queue and will be returned by the next `pop orders` call).*

---

## 🌐 Cluster Pairing & P2P Sharding

`wheelmq` is **decentralized and leaderless**. It does not require ZooKeeper, etcd, or Consul.

### Method A: At Node Startup (via `--peer`)
Start a second node on port `7001` and connect it to `node-1`:
```bash
./bin/wheelmq --id node-2 --port 7001 --peer node-1,127.0.0.1,7000
```

### Method B: Dynamically via CLI (`JOIN`)
If a node is already running, join a peer on the fly:
```bash
./bin/wheelmq-cli -p 7000 join node-2 127.0.0.1 7001
```
*Response:* `OK peer_added id=node-2 nodes=2 vnodes=256`

### Lookup Queue Ownership (`RING_LOOKUP`)
Every queue or key is deterministically assigned to a node using MurmurHash3 across 128 virtual nodes:
```bash
./bin/wheelmq-cli -p 7000 ring_lookup orders
```
*Response:* `OWNER node=node-2 addr=127.0.0.1:7001 is_local=false`

---

## 🐘 Handling Large Messages

### 1. In-Memory Payloads (Up to 64 KB per message)
`wheelmq` supports payloads up to **64 KB** inline in memory by default. This accommodates complex JSON objects, webhook payloads, and batch commands without truncation.

### 2. Large Datasets & Multi-Megabyte Blobs
In high-throughput distributed systems, sending massive files (e.g. 100 MB videos, 4K images, raw database dumps) directly through queue memory is an anti-pattern that causes memory bloat and head-of-line blocking.

The recommended industry approach is the **Claim-Check Pattern**:
1. Store the large artifact in dedicated object storage (e.g., S3, MinIO, or local disk).
2. Push only the metadata and reference URI to `wheelmq`:
```bash
./bin/wheelmq-cli -p 7000 push video-transcode 0 "{\"file_uri\": \"s3://bucket/4k_footage.mp4\", \"target_res\": \"1080p\"}"
```
The worker dequeues the small claim ticket in microseconds and streams the large payload directly from storage during execution.

---

## 📖 Full Command & Protocol Reference

`wheelmq` uses a lightweight, line-delimited text protocol:

| CLI Command | Wire Protocol Syntax | Description |
| :--- | :--- | :--- |
| `push <queue> <delay_ms> <payload>` | `PUSH <queue> <delay_ms> <payload...>` | Enqueue a message. If `delay_ms=0` goes to FIFO; if `delay_ms>0` enters Timing Wheel. |
| `pop <queue>` | `POP <queue>` | Dequeue the oldest ready message from FIFO. |
| `fail <queue> <msg_id> [reason]` | `FAIL <queue> <msg_id> [reason...]` | Mark message as failed; transfers to DLQ. |
| `dlq_list <queue>` | `DLQ_LIST <queue>` | Inspect dead-lettered messages and error reasons. |
| `dlq_replay <queue> <msg_id>` | `DLQ_REPLAY <queue> <msg_id>` | Reintroduce a dead-lettered job back to active FIFO. |
| `join <id> <ip> <port>` | `JOIN <id> <ip> <port>` | Register a cluster peer into the Consistent Hash Ring. |
| `ring_lookup <key>` | `RING_LOOKUP <key>` | Query which cluster node owns a given queue or key. |
| `stats` | `STATS` | Output active queues, ready count, DLQ count, timers, and cluster node count. |
| *(ping)* | `PING` | Connection health check (returns `PONG`). |

---

## ⚡ Performance Benchmarks

* **Binary Size:** **~36 KB** (clean static footprint, zero runtime bloat).
* **Memory Footprint:** ~256 bytes per job (~25 MB RAM for 100,000 active scheduled tasks).
* **Algorithm Complexity:** **$O(1)$ amortized** for insertion, cascading, and expiration.
* **Build Time:** Compiles in **< 0.5 seconds** with `clang` or `gcc`.

---

## 📜 License
This project is open-sourced under the **MIT License**. Free for commercial and community use.
