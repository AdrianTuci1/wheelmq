# 🎡 wheelmq

> **High-Performance P2P Distributed Timing & Queue Engine written in pure C11.**  
> Zero external dependencies · ~53 KB static binary · $O(1)$ Hierarchical Timing Wheel · Consistent Hash Ring Sharding · Dead Letter Queue (DLQ) · Embedded HTTP REST API & Webhook Dispatcher

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](#)
[![Language](https://img.shields.io/badge/language-C11-blue.svg)](#)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](#)
[![Zero-Dependencies](https://img.shields.io/badge/dependencies-zero-orange.svg)](#)

---

## 📑 Table of Contents
1. [What is wheelmq?](#-what-is-wheelmq)
2. [Dual Operating Modes: Pull vs Push (Webhooks)](#-dual-operating-modes-pull-vs-push-webhooks)
3. [Download & Installation](#-download--installation)
4. [HTTP REST API Reference (curl examples)](#-http-rest-api-reference)
5. [TCP Wire Protocol & CLI Usage](#-tcp-wire-protocol--cli-usage)
6. [Dead Letter Queue (DLQ) & Failure Recovery](#-dead-letter-queue-dlq--failure-recovery)
7. [Cluster Pairing & P2P Sharding](#-cluster-pairing--p2p-sharding)
8. [Handling Large Messages](#-handling-large-messages)
9. [Performance Benchmarks](#-performance-benchmarks)

---

## 💡 What is wheelmq?

In production, traditional message brokers (Kafka, RabbitMQ) struggle with **delayed / scheduled tasks** at high throughput, while legacy cron daemons are notorious single points of failure (SPOF). To achieve high availability, DevOps teams are often forced to operate heavy distributed clusters (ZooKeeper, etcd, Redis, or PostgreSQL).

**`wheelmq`** bundles the essential pillars of resilient backend infrastructure into a **single, ~53 KB static C binary**:

1. **Strict FIFO Message Queue** — Reliable, in-order task distribution to workers.
2. **Hierarchical Timing Wheel ($O(1)$)** — Tracks and cascades millions of future tasks (due in milliseconds, minutes, or days) with zero database scanning overhead.
3. **Dead Letter Queue (DLQ) & Auto-Retry** — When downstream workers or webhooks fail, tasks are preserved with contextual error logs and can be inspected and replayed on demand.
4. **P2P Sharding via Consistent Hash Ring** — 1 node owns 100% of the ring; multiple nodes dynamically partition queues with zero master bottleneck.
5. **Embedded HTTP REST API & Webhooks** — Accepts standard JSON `curl` requests on the same port and can autonomously push webhooks to target endpoints upon task expiration.

```
                                PRODUCER
                                   │
              POST /push (JSON) or TCP PUSH queue [delay_ms]
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
                     ┌─────────────┴─────────────┐
               Has Webhook URL?               Pull Mode
                    YES                          NO
                     │                           │
          [HTTP POST Dispatch]             [Worker Pop]
                     │                           │
         ┌───────────┴───────────┐         [Worker Ack]
      SUCCESS                 FAILURE
         │                       │
   [Job Done]              Retries < Max?
                           ┌─────┴─────┐
                          YES          NO
                           │           │
                     [Re-queue]   ┌────▼────┐
                     (Backoff)    │   DLQ   │
                                  └─────────┘
```

---

## 🔄 Dual Operating Modes: Pull vs Push (Webhooks)

`wheelmq` supports two distinct consumption paradigms simultaneously:

* **Pull Mode (Traditional):** Workers periodically fetch jobs using `GET /pop?queue=orders` or the high-speed TCP wire command `POP orders`.
* **Push Mode (Webhook Dispatcher):** Producers attach a `webhook_url` to the task. When the scheduled delay expires, `wheelmq` **autonomously dispatches an HTTP POST** with the payload to the target service (Stripe / AWS EventBridge style). If the endpoint returns non-2xx or times out, `wheelmq` automatically retries with exponential backoff and escalates to the Dead Letter Queue (DLQ).

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

Binaries are generated in `bin/`:
* `bin/wheelmq` — The unified daemon (~53 KB).
* `bin/wheelmq-cli` — The standalone command-line client utility (~33 KB).

---

## 🌐 HTTP REST API Reference

`wheelmq` features an embedded zero-overhead HTTP/1.1 server. You can interact with it using `curl`, Postman, or native HTTP clients in any programming language (Python, Node.js, Go, Rust, Java).

### Start the Server:
```bash
./bin/wheelmq --id node-1 --port 7000
```

### 1. Enqueue an Immediate Task (FIFO)
```bash
curl -X POST http://localhost:7000/push \
  -H "Content-Type: application/json" \
  -d '{
    "queue": "orders",
    "delay_ms": 0,
    "payload": {"order_id": 1001, "amount": 49.99}
  }'
```
*Response:*
```json
{"status":"ready","id":1,"queue":"orders","has_webhook":false}
```

### 2. Schedule a Delayed Task ($O(1)$ Timing Wheel)
```bash
curl -X POST http://localhost:7000/push \
  -H "Content-Type: application/json" \
  -d '{
    "queue": "orders",
    "delay_ms": 5000,
    "payload": {"order_id": 1002, "reminder": true}
  }'
```
*Response:*
```json
{"status":"scheduled","id":2,"queue":"orders","delay_ms":5000,"has_webhook":false}
```

### 3. Schedule an Autonomous Webhook Dispatch (Push Mode)
Schedule an HTTP POST to be dispatched to your webhook receiver in 10 seconds:
```bash
curl -X POST http://localhost:7000/push \
  -H "Content-Type: application/json" \
  -d '{
    "queue": "notifications",
    "delay_ms": 10000,
    "webhook_url": "http://127.0.0.1:8080/events/order-completed",
    "payload": {"order_id": 1003, "customer_email": "alice@example.com"}
  }'
```
*Response:*
```json
{"status":"scheduled","id":3,"queue":"notifications","delay_ms":10000,"has_webhook":true}
```

### 4. Consume Next Task (`POP`)
```bash
curl "http://localhost:7000/pop?queue=orders"
```
*Response:*
```json
{
  "status": "ok",
  "id": 1,
  "queue": "orders",
  "retry_count": 0,
  "payload": {"order_id": 1001, "amount": 49.99}
}
```

### 5. Inspect Cluster & Queue Statistics
```bash
curl http://localhost:7000/stats
```
*Response:*
```json
{
  "node_id": "node-1",
  "queues": 2,
  "ready_messages": 1,
  "dlq_messages": 0,
  "active_timers": 1,
  "ring_nodes": 1,
  "ring_vnodes": 128
}
```

### 6. Inspect Dead Letter Queue (DLQ)
```bash
curl "http://localhost:7000/dlq?queue=orders"
```
*Response:*
```json
{
  "queue": "orders",
  "count": 1,
  "messages": [
    {"id": 404, "retries": 3, "reason": "Webhook returned HTTP 500"}
  ]
}
```

### 7. Replay a Failed Message from DLQ
```bash
curl -X POST http://localhost:7000/dlq/replay \
  -H "Content-Type: application/json" \
  -d '{"queue": "orders", "id": 404}'
```
*Response:*
```json
{"status":"ok","replayed_id":404}
```

---

## ⚡ TCP Wire Protocol & CLI Usage

For ultra-low latency internal worker loops, `wheelmq` supports a high-speed line-delimited ASCII protocol (Redis style):

```bash
# Push an immediate task
./bin/wheelmq-cli -p 7000 push orders 0 "{\"order_id\": 1001}"

# Push a delayed task
./bin/wheelmq-cli -p 7000 push orders 5000 "{\"order_id\": 1002}"

# Push a webhook task
./bin/wheelmq-cli -p 7000 push orders 5000 http://127.0.0.1:8080/hook "{\"order_id\": 1003}"

# Consume task from queue
./bin/wheelmq-cli -p 7000 pop orders

# Mark task as failed (escalates to DLQ)
./bin/wheelmq-cli -p 7000 fail orders 101 "Database deadlocked"

# List DLQ entries
./bin/wheelmq-cli -p 7000 dlq_list orders

# Replay dead task
./bin/wheelmq-cli -p 7000 dlq_replay orders 101
```

---

## 🛡️ Dead Letter Queue (DLQ) & Failure Recovery

When downstream processing or webhooks fail:
1. **Transient Errors:** Tasks are retried with exponential backoff ($1\text{s}, 2\text{s}, 4\text{s}$).
2. **Persistent Failures:** Once `max_retries` (default: 3) is reached, the task is transferred to the Dead Letter Queue (DLQ) alongside the error description.
3. **Zero Loss & Replay:** Operators can inspect failed tasks via `GET /dlq` and restore them to active processing with a single `POST /dlq/replay`.

---

## 🌐 Cluster Pairing & P2P Sharding

`wheelmq` is **decentralized and leaderless**. It does not require ZooKeeper, etcd, or Consul.

### Join at Node Startup:
```bash
./bin/wheelmq --id node-2 --port 7001 --peer node-1,127.0.0.1,7000
```

### Join on the Fly via CLI:
```bash
./bin/wheelmq-cli -p 7000 join node-2 127.0.0.1 7001
```

### Lookup Queue Partition Ownership:
```bash
curl "http://localhost:7000/ring?key=orders"
```
*Response:*
```json
{
  "key": "orders",
  "owner_node": "node-2",
  "address": "127.0.0.1:7001",
  "is_local": false
}
```

---

## 🐘 Handling Large Messages

* **In-Memory Payloads (Up to 64 KB per message):** `wheelmq` natively supports payloads up to **64 KB** inline in RAM.
* **Large Datasets & Files (Claim-Check Pattern):** For multi-megabyte or gigabyte files, store the blob in S3/MinIO and pass the reference URI:
```bash
curl -X POST http://localhost:7000/push -d '{
  "queue": "transcoding",
  "delay_ms": 0,
  "payload": {"file_uri": "s3://media-bucket/raw_4k.mp4"}
}'
```

---

## ⚡ Performance Benchmarks

* **Binary Size:** **~53 KB** (includes TCP engine, HTTP REST API, and Webhook dispatcher).
* **Memory Footprint:** ~256 bytes per job (~25 MB RAM for 100,000 active scheduled tasks).
* **Algorithm Complexity:** **$O(1)$ amortized** for insertion, cascading, and expiration.
* **Build Time:** Compiles in **< 0.5 seconds** with `clang` or `gcc`.

---

## 📜 License
This project is open-sourced under the **MIT License**. Free for commercial and community use.
