#define _POSIX_C_SOURCE 200809L

#include "http.h"
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

bool http_is_request(const char *buf, size_t len) {
    if (len < 4) return false;
    return (strncmp(buf, "GET ", 4) == 0 ||
            strncmp(buf, "POST ", 5) == 0 ||
            strncmp(buf, "PUT ", 4) == 0 ||
            strncmp(buf, "DELETE ", 7) == 0 ||
            strncmp(buf, "OPTIONS ", 8) == 0);
}

bool http_parse(const char *buf, size_t len, http_request_t *req) {
    (void)len;
    memset(req, 0, sizeof(http_request_t));

    char full_path[256];
    if (sscanf(buf, "%15s %255s", req->method, full_path) != 2) {
        return false;
    }

    // Split path and query string (e.g. /pop?queue=orders)
    char *qmark = strchr(full_path, '?');
    if (qmark) {
        *qmark = '\0';
        strncpy(req->query, qmark + 1, sizeof(req->query) - 1);
    }
    strncpy(req->path, full_path, sizeof(req->path) - 1);

    // Locate end of headers "\r\n\r\n"
    const char *body_start = strstr(buf, "\r\n\r\n");
    if (body_start) {
        req->body = body_start + 4;
        req->body_len = strlen(req->body);
    } else {
        const char *body_start_lf = strstr(buf, "\n\n");
        if (body_start_lf) {
            req->body = body_start_lf + 2;
            req->body_len = strlen(req->body);
        }
    }

    return true;
}

bool http_query_get(const char *query, const char *key, char *val_out, size_t max_val) {
    if (!query || !key || !val_out) return false;

    size_t key_len = strlen(key);
    const char *p = query;

    while (*p) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *val_start = p + key_len + 1;
            const char *val_end = strchr(val_start, '&');
            size_t vlen = val_end ? (size_t)(val_end - val_start) : strlen(val_start);

            if (vlen >= max_val) vlen = max_val - 1;
            memcpy(val_out, val_start, vlen);
            val_out[vlen] = '\0';
            return true;
        }
        p = strchr(p, '&');
        if (!p) break;
        p++;
    }
    return false;
}

/**
 * Extract a simple string field from a JSON string: "field": "value" or "field": 123
 */
static bool json_extract_string(const char *json, const char *key, char *val_out, size_t max_val) {
    if (!json || !key || !val_out) return false;

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) return false;

    pos += strlen(pattern);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;

    if (*pos == '\"') {
        pos++;
        const char *end = strchr(pos, '\"');
        if (!end) return false;
        size_t len = (size_t)(end - pos);
        if (len >= max_val) len = max_val - 1;
        memcpy(val_out, pos, len);
        val_out[len] = '\0';
        return true;
    } else {
        // Unquoted numeric or boolean value
        size_t len = 0;
        while (pos[len] && pos[len] != ',' && pos[len] != '}' && pos[len] != ' ' && pos[len] != '\r' && pos[len] != '\n') {
            len++;
        }
        if (len == 0) return false;
        if (len >= max_val) len = max_val - 1;
        memcpy(val_out, pos, len);
        val_out[len] = '\0';
        return true;
    }
}

static void send_http_json(char *resp_out, size_t resp_max, int status_code, const char *status_msg, const char *json_body) {
    size_t body_len = strlen(json_body);
    snprintf(resp_out, resp_max,
             "HTTP/1.1 %d %s\r\n"
             "Server: wheelmq/1.0\r\n"
             "Content-Type: application/json; charset=utf-8\r\n"
             "Content-Length: %zu\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Connection: close\r\n\r\n"
             "%s",
             status_code, status_msg, body_len, json_body);
}

void http_route(wheelmq_server_t *srv, const http_request_t *req, char *resp_out, size_t resp_max) {
    pthread_mutex_lock(&srv->lock);

    if (strcmp(req->path, "/") == 0) {
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"engine\":\"wheelmq\",\"version\":\"1.0\",\"status\":\"online\",\"node_id\":\"%s\"}",
                 srv->node_id);
        send_http_json(resp_out, resp_max, 200, "OK", json);
    }
    else if (strcmp(req->path, "/stats") == 0) {
        size_t total_ready = 0;
        size_t total_dlq = 0;
        for (size_t i = 0; i < srv->queue_count; i++) {
            total_ready += queue_ready_count(&srv->queues[i]);
            total_dlq += queue_dlq_count(&srv->queues[i]);
        }
        char json[512];
        snprintf(json, sizeof(json),
                 "{\"node_id\":\"%s\",\"queues\":%zu,\"ready_messages\":%zu,\"dlq_messages\":%zu,"
                 "\"active_timers\":%zu,\"ring_nodes\":%u,\"ring_vnodes\":%u}",
                 srv->node_id, srv->queue_count, total_ready, total_dlq,
                 srv->wheel.total_timer_count, srv->ring.node_count, srv->ring.vnode_count);
        send_http_json(resp_out, resp_max, 200, "OK", json);
    }
    else if (strcmp(req->path, "/push") == 0) {
        char queue[64] = "default";
        char delay_str[32] = "0";
        char webhook[256] = "";
        char payload[4096] = "{}";

        if (req->body && req->body_len > 0) {
            json_extract_string(req->body, "queue", queue, sizeof(queue));
            json_extract_string(req->body, "delay_ms", delay_str, sizeof(delay_str));
            json_extract_string(req->body, "webhook_url", webhook, sizeof(webhook));
            json_extract_string(req->body, "payload", payload, sizeof(payload));
        } else {
            http_query_get(req->query, "queue", queue, sizeof(queue));
            http_query_get(req->query, "delay_ms", delay_str, sizeof(delay_str));
            http_query_get(req->query, "webhook_url", webhook, sizeof(webhook));
            http_query_get(req->query, "payload", payload, sizeof(payload));
        }

        uint64_t delay_ms = strtoull(delay_str, NULL, 10);
        uint64_t now = get_time_ms();
        uint64_t msg_id = ++srv->next_msg_id;
        uint64_t scheduled_at = now + delay_ms;

        message_t *msg = message_create(msg_id, queue, payload, strlen(payload), scheduled_at, 3);
        if (strlen(webhook) > 0) {
            message_set_webhook(msg, webhook);
        }

        if (!msg) {
            send_http_json(resp_out, resp_max, 500, "Internal Server Error", "{\"error\":\"out_of_memory\"}");
        } else if (delay_ms > 0) {
            wheel_add(&srv->wheel, msg);
            char json[512];
            snprintf(json, sizeof(json),
                     "{\"status\":\"scheduled\",\"id\":%llu,\"queue\":\"%s\",\"delay_ms\":%llu,\"has_webhook\":%s}",
                     (unsigned long long)msg_id, queue, (unsigned long long)delay_ms,
                     strlen(webhook) > 0 ? "true" : "false");
            send_http_json(resp_out, resp_max, 200, "OK", json);
        } else {
            queue_t *q = server_find_or_create_queue(srv, queue);
            if (q) {
                queue_enqueue_ready(q, msg);
                char json[512];
                snprintf(json, sizeof(json),
                         "{\"status\":\"ready\",\"id\":%llu,\"queue\":\"%s\",\"has_webhook\":%s}",
                         (unsigned long long)msg_id, queue,
                         strlen(webhook) > 0 ? "true" : "false");
                send_http_json(resp_out, resp_max, 200, "OK", json);
            } else {
                message_free(msg);
                send_http_json(resp_out, resp_max, 507, "Insufficient Storage", "{\"error\":\"max_queues_reached\"}");
            }
        }
    }
    else if (strcmp(req->path, "/pop") == 0) {
        char queue[64] = "default";
        http_query_get(req->query, "queue", queue, sizeof(queue));

        queue_t *q = server_find_or_create_queue(srv, queue);
        message_t *msg = q ? queue_dequeue_ready(q) : NULL;

        if (msg) {
            char json[SERVER_IO_BUF_SIZE / 2];
            if (msg->payload_len > 0 && (msg->payload[0] == '{' || msg->payload[0] == '[')) {
                snprintf(json, sizeof(json),
                         "{\"status\":\"ok\",\"id\":%llu,\"queue\":\"%s\",\"retry_count\":%u,\"payload\":%.*s}",
                         (unsigned long long)msg->id, msg->queue_name, msg->retry_count,
                         (int)msg->payload_len, (char *)msg->payload);
            } else {
                snprintf(json, sizeof(json),
                         "{\"status\":\"ok\",\"id\":%llu,\"queue\":\"%s\",\"retry_count\":%u,\"payload\":\"%.*s\"}",
                         (unsigned long long)msg->id, msg->queue_name, msg->retry_count,
                         (int)msg->payload_len, (char *)msg->payload);
            }
            message_free(msg);
            send_http_json(resp_out, resp_max, 200, "OK", json);
        } else {
            char json[128];
            snprintf(json, sizeof(json), "{\"status\":\"empty\",\"queue\":\"%s\"}", queue);
            send_http_json(resp_out, resp_max, 200, "OK", json);
        }
    }
    else if (strcmp(req->path, "/dlq") == 0) {
        char queue[64] = "default";
        http_query_get(req->query, "queue", queue, sizeof(queue));

        queue_t *q = server_find_or_create_queue(srv, queue);
        size_t count = q ? queue_dlq_count(q) : 0;

        char json[4096];
        size_t offset = snprintf(json, sizeof(json), "{\"queue\":\"%s\",\"count\":%zu,\"messages\":[", queue, count);

        if (q && count > 0) {
            message_t *curr = q->dlq.head;
            bool first = true;
            while (curr && offset < sizeof(json) - 128) {
                if (!first) offset += snprintf(json + offset, sizeof(json) - offset, ",");
                offset += snprintf(json + offset, sizeof(json) - offset,
                                   "{\"id\":%llu,\"retries\":%u,\"reason\":\"%s\"}",
                                   (unsigned long long)curr->id, curr->retry_count, curr->error_msg);
                first = false;
                curr = curr->next;
            }
        }
        snprintf(json + offset, sizeof(json) - offset, "]}");
        send_http_json(resp_out, resp_max, 200, "OK", json);
    }
    else if (strcmp(req->path, "/dlq/replay") == 0) {
        char queue[64] = "default";
        char id_str[32] = "0";

        if (req->body && req->body_len > 0) {
            json_extract_string(req->body, "queue", queue, sizeof(queue));
            json_extract_string(req->body, "id", id_str, sizeof(id_str));
        } else {
            http_query_get(req->query, "queue", queue, sizeof(queue));
            http_query_get(req->query, "id", id_str, sizeof(id_str));
        }

        uint64_t msg_id = strtoull(id_str, NULL, 10);
        queue_t *q = server_find_or_create_queue(srv, queue);

        if (q && queue_replay_dlq(q, msg_id)) {
            char json[128];
            snprintf(json, sizeof(json), "{\"status\":\"ok\",\"replayed_id\":%llu}", (unsigned long long)msg_id);
            send_http_json(resp_out, resp_max, 200, "OK", json);
        } else {
            send_http_json(resp_out, resp_max, 404, "Not Found", "{\"error\":\"message_not_found_in_dlq\"}");
        }
    }
    else if (strcmp(req->path, "/ring") == 0) {
        char key[64] = "default";
        http_query_get(req->query, "key", key, sizeof(key));

        const cluster_node_t *owner = hashring_lookup(&srv->ring, key, strlen(key));
        if (owner) {
            char json[256];
            snprintf(json, sizeof(json),
                     "{\"key\":\"%s\",\"owner_node\":\"%s\",\"address\":\"%s:%u\",\"is_local\":%s}",
                     key, owner->id, owner->address, owner->port,
                     (strcmp(owner->id, srv->node_id) == 0) ? "true" : "false");
            send_http_json(resp_out, resp_max, 200, "OK", json);
        } else {
            send_http_json(resp_out, resp_max, 503, "Service Unavailable", "{\"error\":\"no_cluster_nodes\"}");
        }
    }
    else {
        send_http_json(resp_out, resp_max, 404, "Not Found", "{\"error\":\"route_not_found\"}");
    }

    pthread_mutex_unlock(&srv->lock);
}
