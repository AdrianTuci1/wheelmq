#ifndef WHEELMQ_HTTP_H
#define WHEELMQ_HTTP_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Forward declaration of wheelmq_server_t to avoid circular dependency.
 */
typedef struct wheelmq_server_s wheelmq_server_t;

/**
 * Parsed incoming HTTP request.
 */
typedef struct {
    char method[16];           /**< "GET", "POST", "OPTIONS", etc. */
    char path[128];            /**< Request URL path without query string (e.g. "/push") */
    char query[256];           /**< Query string parameter buffer (e.g. "queue=orders") */
    const char *body;          /**< Pointer to start of request body in raw buffer */
    size_t body_len;           /**< Content length of the request body */
} http_request_t;

/**
 * Check if a raw socket buffer contains an HTTP request (starts with GET, POST, etc.).
 *
 * @param buf Raw buffer received from socket.
 * @param len Buffer length in bytes.
 * @return true if detected as HTTP, false for native TCP wire protocol.
 */
bool http_is_request(const char *buf, size_t len);

/**
 * Parse a raw HTTP/1.1 request into the http_request_t structure.
 *
 * @param buf Raw buffer.
 * @param len Buffer length.
 * @param req Output structure.
 * @return true on successful parse, false if malformed.
 */
bool http_parse(const char *buf, size_t len, http_request_t *req);

/**
 * Extract a query parameter value by key from a query string (e.g. "queue=orders").
 *
 * @param query Query string.
 * @param key Key name to look for.
 * @param val_out Output buffer for value.
 * @param max_val Maximum capacity of output buffer.
 * @return true if key found, false otherwise.
 */
bool http_query_get(const char *query, const char *key, char *val_out, size_t max_val);

/**
 * Route an HTTP request and write an HTTP/1.1 JSON response into resp_out.
 *
 * Handled Routes:
 * - GET  /           -> Welcome & Health Status
 * - GET  /stats      -> Cluster, Queue & Timing Wheel statistics
 * - POST /push       -> Enqueue task (immediate, scheduled, or webhook)
 * - GET  /pop        -> Dequeue next task from queue (?queue=name)
 * - GET  /dlq        -> List dead-lettered tasks (?queue=name)
 * - POST /dlq/replay -> Replay dead-letter task (?queue=name&id=123)
 * - POST /fail       -> Manually fail a task (?queue=name&id=123&reason=...)
 * - GET  /ring       -> Check cluster partition ownership (?key=name)
 *
 * @param srv Pointer to wheelmq_server_t instance.
 * @param req Parsed HTTP request.
 * @param resp_out Output buffer for HTTP response.
 * @param resp_max Maximum capacity of response buffer.
 */
void http_route(wheelmq_server_t *srv, const http_request_t *req, char *resp_out, size_t resp_max);

#endif // WHEELMQ_HTTP_H
