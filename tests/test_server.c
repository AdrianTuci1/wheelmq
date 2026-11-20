#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    printf("[TEST] Server Engine (FIFO + Delayed Wheel + DLQ + HashRing + Large Payloads)...\n");

    wheelmq_server_t srv;
    int rc = server_init(&srv, "node-primary", "127.0.0.1", 7000, "");
    assert(rc == 0);

    // 1. Test Hash Ring within server
    assert(srv.ring.node_count == 1);
    const cluster_node_t *owner = hashring_lookup(&srv.ring, "orders_queue", 12);
    assert(owner != NULL && strcmp(owner->id, "node-primary") == 0);
    printf("  [PASS] Hash Ring initialized with primary node\n");

    // 2. Test peer joining
    hashring_add_node(&srv.ring, "node-secondary", "127.0.0.1", 7001);
    assert(srv.ring.node_count == 2);
    assert(srv.ring.vnode_count == 2 * HASHRING_VNODES_PER_NODE);
    printf("  [PASS] Cluster peer joining & vnode expansion verified\n");

    // 3. Test queue creation and FIFO
    queue_t *q = &srv.queues[srv.queue_count++];
    queue_init(q, "orders");

    message_t *m1 = message_create(1, "orders", "order_data_1", 12, 0, 3);
    queue_enqueue_ready(q, m1);
    assert(queue_ready_count(q) == 1);

    message_t *pop1 = queue_dequeue_ready(q);
    assert(pop1 != NULL && pop1->id == 1);
    assert(queue_ready_count(q) == 0);
    printf("  [PASS] In-memory FIFO queue enqueue and dequeue\n");

    // 4. Test DLQ transition & replay
    queue_move_to_dlq(q, pop1, "Downstream timeout");
    assert(queue_dlq_count(q) == 1);

    bool replayed = queue_replay_dlq(q, 1);
    assert(replayed == true);
    assert(queue_dlq_count(q) == 0);
    assert(queue_ready_count(q) == 1);

    message_t *re_pop = queue_dequeue_ready(q);
    assert(re_pop->id == 1);
    message_free(re_pop);
    printf("  [PASS] Dead Letter Queue failure transition & replay\n");

    // 5. Test Large Payload Support (e.g. 16 KB JSON message)
    size_t large_size = 16384;
    char *large_payload = (char *)malloc(large_size);
    memset(large_payload, 'X', large_size - 1);
    large_payload[large_size - 1] = '\0';

    message_t *m_large = message_create(999, "large_queue", large_payload, large_size, 0, 1);
    assert(m_large != NULL);
    assert(m_large->payload_len == large_size);
    assert(memcmp(m_large->payload, large_payload, large_size) == 0);
    message_free(m_large);
    free(large_payload);
    printf("  [PASS] Large payload (16 KB) created and freed with zero loss\n");

    // 6. Test Timing Wheel integration
    uint64_t now = srv.wheel.current_time_ms;
    message_t *m_delayed = message_create(2, "orders", "delayed_order", 13, now + 100, 3);
    wheel_add(&srv.wheel, m_delayed);
    assert(srv.wheel.total_timer_count == 1);

    msg_list_t expired;
    memset(&expired, 0, sizeof(msg_list_t));

    // At now + 50ms: not expired
    size_t fired = wheel_advance(&srv.wheel, now + 50, &expired);
    assert(fired == 0);

    // At now + 120ms: expired!
    fired = wheel_advance(&srv.wheel, now + 120, &expired);
    assert(fired == 1);
    assert(expired.head->id == 2);
    message_free(expired.head);
    printf("  [PASS] Timing Wheel delayed dispatch verified\n");

    server_destroy(&srv);
    printf("[SUCCESS] All Server Engine tests passed!\n");
    return 0;
}
