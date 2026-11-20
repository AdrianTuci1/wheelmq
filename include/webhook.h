#ifndef WHEELMQ_WEBHOOK_H
#define WHEELMQ_WEBHOOK_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * ============================================================================
 * ARCHITECTURAL DESIGN DECISIONS: WEBHOOK DISPATCHER (PUSH-MODE QUEUING)
 * ============================================================================
 *
 * 1. PUSH-MODE VS PULL-MODE:
 *    - Traditional message brokers (RabbitMQ/Kafka) rely on workers polling the
 *      broker (pull model). This wastes CPU on empty polls and requires workers
 *      to maintain persistent connections.
 *    - Push-mode queuing (pioneered by Stripe webhooks and AWS EventBridge API
 *      Destinations) flips the paradigm: wheelmq pushes the event directly to
 *      an HTTP endpoint when due.
 *
 * 2. AUTOMATIC RETRY & EXPONENTIAL BACKOFF:
 *    - If the target webhook returns 5xx (Server Error) or network times out,
 *      wheelmq automatically recalculates a delayed retry timestamp:
 *      delay = base_backoff * (2 ^ retry_count).
 *    - The task is re-inserted into the Hierarchical Timing Wheel in O(1).
 *    - Once max_retries is reached, the task automatically moves to DLQ.
 * ============================================================================
 */

/**
 * Result of a webhook dispatch attempt.
 */
typedef struct {
    int http_status;           /**< HTTP response status code (e.g. 200, 404, 500) or -1 on network failure */
    char error_desc[128];      /**< Human-readable error description if failed */
    bool success;              /**< true if HTTP 2xx received */
} webhook_result_t;

/**
 * Dispatch an outbound HTTP POST request to a webhook target.
 *
 * @param url Target HTTP URL (e.g. "http://127.0.0.1:8080/events/order").
 * @param payload Raw payload data to transmit in request body.
 * @param payload_len Size of payload in bytes.
 * @param timeout_ms Socket send/recv timeout in milliseconds.
 * @return webhook_result_t containing response code and error details.
 */
webhook_result_t webhook_dispatch(const char *url, const void *payload, size_t payload_len, uint32_t timeout_ms);

#endif // WHEELMQ_WEBHOOK_H
