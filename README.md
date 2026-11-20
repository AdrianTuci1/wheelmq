# 🎡 wheelmq

> **High-Performance P2P Distributed Timing & Queue Engine written in pure C11.**  
> Zero external dependencies · ~36 KB static binary · $O(1)$ Hierarchical Timing Wheel · Consistent Hash Ring Sharding · Dead Letter Queue (DLQ)

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](#)
[![Language](https://img.shields.io/badge/language-C11-blue.svg)](#)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](#)
[![Zero-Dependencies](https://img.shields.io/badge/dependencies-zero-orange.svg)](#)

---

## 💡 The Core Problem

Traditional message queues (Kafka, RabbitMQ) struggle with **delayed / scheduled tasks** at scale. Scheduled cron daemons are notorious single points of failure (SPOF), and heavy distributed coordinators (ZooKeeper, etcd, Redis Cluster) introduce massive operational overhead.

**`wheelmq`** combines the four essential primitives of mission-critical backend systems into a **single, lightweight C binary**:

1. **Strict FIFO Message Queue** — In-order worker dispatch with acknowledgment.
2. **Hierarchical Timing Wheel ($O(1)$)** — Handles millions of concurrent delayed tasks with millisecond precision without scanning tables.
3. **Dead Letter Queue (DLQ) & Auto-Retry** — Resilient failure handling with automatic backoff retry and one-click replay.
4. **P2P Sharding via Consistent Hash Ring** — 1 node owns 100% of the ring; $N$ nodes partition queues dynamically with zero single point of failure (SPOF).

---

## 🏛️ Architecture Overview

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
                     │ Has Delay?                │
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

## ⚡ Performance & Resource Footprint

* **Binary Size:** **~36 KB** (no JVM, no runtime, no node_modules).
* **Memory Footprint:** ~256 bytes per job (~25 MB RAM for 100,000 active scheduled tasks).
* **Scheduling Complexity:** **$O(1)$ amortized** for insertion, cascading, and expiration.
* **Build Time:** Compiles in **< 0.5 seconds** with `clang` or `gcc`.

---

## 🚀 Quickstart

### 1. Build
```bash
git clone https://github.com/AdrianTuci1/wheelmq.git
cd wheelmq
make all
```

### 2. Run Test Suite
```bash
make test
```

### 3. Start a Node
```bash
# Start primary node on port 7000
./bin/wheelmq --id node-1 --port 7000
```

### 4. Interact via CLI (`wheelmq-cli`)
```bash
# Push an immediate task into FIFO
./bin/wheelmq-cli -p 7000 push orders 0 "order_id:101"

# Push a delayed task (triggers in 5,000 ms = 5s)
./bin/wheelmq-cli -p 7000 push orders 5000 "order_id:102_delayed"

# Consume task from queue
./bin/wheelmq-cli -p 7000 pop orders

# Inspect cluster stats
./bin/wheelmq-cli -p 7000 stats
```

---

## 🛡️ Dead Letter Queue (DLQ) & Failure Recovery

When a job fails processing:
```bash
# Mark job 101 as failed with error context
./bin/wheelmq-cli -p 7000 fail orders 101 "Stripe payment gateway 504 timeout"

# Inspect DLQ
./bin/wheelmq-cli -p 7000 dlq_list orders
# Output:
# DLQ_COUNT 1
#   - id=101 reason="Stripe payment gateway 504 timeout"

# Replay job back to active FIFO once downstream issue is resolved
./bin/wheelmq-cli -p 7000 dlq_replay orders 101
```

---

## 🌐 Wire Protocol Specification

`wheelmq` uses a lightweight, human-readable text protocol:

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `PUSH` | `<queue> <delay_ms> <payload>` | Enqueue task (immediate if delay=0, or into Timing Wheel). |
| `POP` | `<queue>` | Dequeue next ready task from FIFO. |
| `FAIL` | `<queue> <msg_id> [reason]` | Record failure; moves to DLQ when max retries exceeded. |
| `DLQ_LIST` | `<queue>` | Inspect dead-lettered tasks in queue. |
| `DLQ_REPLAY` | `<queue> <msg_id>` | Replay dead task back to ready FIFO queue. |
| `JOIN` | `<peer_id> <ip> <port>` | Add a peer node to the Consistent Hash Ring. |
| `RING_LOOKUP` | `<key>` | Query which node in the cluster owns a queue/key. |
| `STATS` | *(none)* | Return active queues, ready count, DLQ count, timers. |
| `PING` | *(none)* | Health check (`PONG`). |

---

## 📁 Repository Structure

```
wheelmq/
├── Makefile                # Fast build & test automation
├── include/
│   ├── hashring.h          # Consistent Hash Ring with Virtual Nodes
│   ├── queue.h             # FIFO Queue & Dead Letter Queue (DLQ)
│   ├── wheel.h             # 5-Level Hierarchical Timing Wheel (O(1))
│   └── server.h            # TCP / UNIX Domain Socket Server
├── src/
│   ├── hashring.c          # MurmurHash3 & binary search vnode ring
│   ├── queue.c             # O(1) doubly-linked FIFO and DLQ engine
│   ├── wheel.c             # Linux-style multi-tier timer cascading
│   ├── server.c            # Multithreaded daemon engine
│   └── main.c              # Daemon entry point & CLI parser
├── tools/
│   └── cli.c               # Standalone lightweight client utility
├── tests/
│   ├── test_hashring.c     # Hash ring distribution & failover tests
│   ├── test_queue.c        # FIFO ordering & DLQ replay tests
│   ├── test_wheel.c        # Timing precision & cascade tests
│   └── test_server.c       # Full engine integration test
└── README.md
```

---

## 📜 License
MIT License. Free for commercial and open-source use.
