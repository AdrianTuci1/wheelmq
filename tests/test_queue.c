#include "queue.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("[TEST] FIFO Queue and Dead Letter Queue (DLQ)...\n");

    queue_t q;
    queue_init(&q, "billing-events");

    assert(queue_ready_count(&q) == 0);
    assert(queue_dlq_count(&q) == 0);

    // 1. Enqueue 3 messages
    const char *payload1 = "{\"order_id\": 101}";
    const char *payload2 = "{\"order_id\": 102}";
    const char *payload3 = "{\"order_id\": 103}";

    message_t *m1 = message_create(1, "billing-events", payload1, strlen(payload1), 0, 3);
    message_t *m2 = message_create(2, "billing-events", payload2, strlen(payload2), 0, 3);
    message_t *m3 = message_create(3, "billing-events", payload3, strlen(payload3), 0, 3);

    queue_enqueue_ready(&q, m1);
    queue_enqueue_ready(&q, m2);
    queue_enqueue_ready(&q, m3);

    assert(queue_ready_count(&q) == 3);

    // 2. Dequeue FIFO check
    message_t *pop1 = queue_dequeue_ready(&q);
    assert(pop1 != NULL && pop1->id == 1);
    assert(memcmp(pop1->payload, payload1, strlen(payload1)) == 0);

    message_t *pop2 = queue_dequeue_ready(&q);
    assert(pop2 != NULL && pop2->id == 2);

    assert(queue_ready_count(&q) == 1);
    printf("  [PASS] FIFO ordering confirmed\n");

    // 3. Move pop1 to DLQ (simulate failed execution)
    queue_move_to_dlq(&q, pop1, "Payment gateway timed out after 3 retries");
    assert(queue_dlq_count(&q) == 1);
    assert(strcmp(pop1->error_msg, "Payment gateway timed out after 3 retries") == 0);
    printf("  [PASS] Failed message moved to DLQ with error context\n");

    // 4. Replay from DLQ
    bool replayed = queue_replay_dlq(&q, 1);
    assert(replayed == true);
    assert(queue_dlq_count(&q) == 0);
    assert(queue_ready_count(&q) == 2); // m3 + replayed m1

    message_t *replayed_msg = queue_dequeue_ready(&q);
    assert(replayed_msg->id == 3); // m3 was next in line
    message_free(replayed_msg);

    replayed_msg = queue_dequeue_ready(&q);
    assert(replayed_msg->id == 1); // replayed m1
    assert(replayed_msg->retry_count == 0);
    message_free(replayed_msg);

    message_free(pop2);

    queue_destroy(&q);
    printf("[SUCCESS] All Queue & DLQ tests passed!\n");
    return 0;
}
