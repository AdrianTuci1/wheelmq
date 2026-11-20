#include "server.h"
#include "http.h"
#include "webhook.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    printf("[TEST] Server Engine (FIFO + Delayed Wheel + DLQ + HashRing + HTTP REST + Webhooks)...\n");

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
    queue_t *q = server_find_or_create_queue(&srv, "orders");
    assert(q != NULL);

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

    // 5. Test Large Payload Support
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

    // 6. Test Webhook metadata attachment
    message_t *m_wh = message_create(888, "webhook_queue", "{\"test\":1}", 10, 0, 3);
    message_set_webhook(m_wh, "http://127.0.0.1:8080/events/webhook");
    assert(strcmp(m_wh->webhook_url, "http://127.0.0.1:8080/events/webhook") == 0);
    message_free(m_wh);
    printf("  [PASS] Outbound webhook target metadata successfully configured\n");

    // 7. Test Inbound HTTP REST API Router
    const char *http_req_stats = "GET /stats HTTP/1.1\r\nHost: localhost:7000\r\n\r\n";
    assert(http_is_request(http_req_stats, strlen(http_req_stats)) == true);

    http_request_t parsed_req;
    bool parsed = http_parse(http_req_stats, strlen(http_req_stats), &parsed_req);
    assert(parsed == true);
    assert(strcmp(parsed_req.method, "GET") == 0);
    assert(strcmp(parsed_req.path, "/stats") == 0);

    char http_resp[4096];
    http_route(&srv, &parsed_req, http_resp, sizeof(http_resp));
    assert(strstr(http_resp, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(http_resp, "\"node_id\":\"node-primary\"") != NULL);
    printf("  [PASS] HTTP GET /stats routed and returned valid JSON\n");

    // Test HTTP POST /push
    const char *http_req_push = "POST /push HTTP/1.1\r\nContent-Length: 60\r\n\r\n"
                                "{\"queue\":\"api_queue\",\"delay_ms\":0,\"payload\":\"http_order\"}";
    assert(http_parse(http_req_push, strlen(http_req_push), &parsed_req) == true);
    http_route(&srv, &parsed_req, http_resp, sizeof(http_resp));
    assert(strstr(http_resp, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(http_resp, "\"status\":\"ready\"") != NULL);
    printf("  [PASS] HTTP POST /push enqueued task via JSON body\n");

    // Test HTTP GET /pop
    const char *http_req_pop = "GET /pop?queue=api_queue HTTP/1.1\r\n\r\n";
    assert(http_parse(http_req_pop, strlen(http_req_pop), &parsed_req) == true);
    http_route(&srv, &parsed_req, http_resp, sizeof(http_resp));
    assert(strstr(http_resp, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(http_resp, "\"payload\":\"http_order\"") != NULL);
    printf("  [PASS] HTTP GET /pop consumed task and returned JSON\n");

    // Test HTTP GET /metrics (Prometheus / Grafana scrape format)
    const char *http_req_metrics = "GET /metrics HTTP/1.1\r\n\r\n";
    assert(http_parse(http_req_metrics, strlen(http_req_metrics), &parsed_req) == true);
    http_route(&srv, &parsed_req, http_resp, sizeof(http_resp));
    assert(strstr(http_resp, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(http_resp, "wheelmq_up{node=\"node-primary\"} 1") != NULL);
    assert(strstr(http_resp, "wheelmq_ready_messages_total{node=\"node-primary\"}") != NULL);
    assert(strstr(http_resp, "wheelmq_delayed_timers_total{node=\"node-primary\"}") != NULL);
    assert(strstr(http_resp, "wheelmq_dead_letter_total{node=\"node-primary\"}") != NULL);
    printf("  [PASS] HTTP GET /metrics exported Prometheus format for Grafana\n");

    server_destroy(&srv);
    printf("[SUCCESS] All Server & HTTP & Webhook Engine tests passed!\n");
    return 0;
}
