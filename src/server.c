#define _POSIX_C_SOURCE 200809L

#include "server.h"
#include "http.h"
#include "webhook.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>


/**
 * Get current wall-clock epoch timestamp in milliseconds.
 */
static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

queue_t *server_find_or_create_queue(wheelmq_server_t *srv, const char *name) {
    for (size_t i = 0; i < srv->queue_count; i++) {
        if (strcmp(srv->queues[i].name, name) == 0) {
            return &srv->queues[i];
        }
    }
    if (srv->queue_count >= MAX_QUEUES) return NULL;

    queue_t *q = &srv->queues[srv->queue_count++];
    queue_init(q, name);
    return q;
}

/**
 * Background ticker thread routine.
 * Ticks the timing wheel every 5ms:
 * - For pull-mode tasks: moves expired timers to their ready FIFO queue.
 * - For push-mode webhook tasks: dispatches HTTP POST with automatic retry and DLQ escalation.
 */
static void *ticker_routine(void *arg) {
    wheelmq_server_t *srv = (wheelmq_server_t *)arg;

    while (srv->running) {
        struct timespec sleep_time = {0, 5 * 1000 * 1000}; // 5ms tick interval
        nanosleep(&sleep_time, NULL);

        uint64_t now = get_time_ms();
        msg_list_t expired;
        memset(&expired, 0, sizeof(msg_list_t));

        pthread_mutex_lock(&srv->lock);
        size_t count = wheel_advance(&srv->wheel, now, &expired);
        pthread_mutex_unlock(&srv->lock);

        if (count > 0) {
            message_t *curr = expired.head;
            while (curr) {
                message_t *next = curr->next;

                if (curr->webhook_url[0] != '\0') {
                    // Push Mode: Dispatch HTTP POST webhook
                    webhook_result_t res = webhook_dispatch(curr->webhook_url, curr->payload, curr->payload_len, 3000);

                    pthread_mutex_lock(&srv->lock);
                    if (res.success) {
                        message_free(curr);
                    } else {
                        curr->retry_count++;
                        if (curr->retry_count < curr->max_retries) {
                            // Exponential backoff: 1s, 2s, 4s...
                            uint64_t backoff_delay = 1000ULL * (1ULL << curr->retry_count);
                            curr->scheduled_at_ms = get_time_ms() + backoff_delay;
                            wheel_add(&srv->wheel, curr);
                        } else {
                            // Max retries exhausted: escalate to DLQ
                            queue_t *q = server_find_or_create_queue(srv, curr->queue_name);
                            if (q) {
                                queue_move_to_dlq(q, curr, res.error_desc);
                            } else {
                                message_free(curr);
                            }
                        }
                    }
                    pthread_mutex_unlock(&srv->lock);
                } else {
                    // Pull Mode: Move to ready FIFO queue
                    pthread_mutex_lock(&srv->lock);
                    queue_t *q = server_find_or_create_queue(srv, curr->queue_name);
                    if (q) {
                        queue_enqueue_ready(q, curr);
                    } else {
                        message_free(curr);
                    }
                    pthread_mutex_unlock(&srv->lock);
                }
                curr = next;
            }
        }
    }
    return NULL;
}

/**
 * Extract the next whitespace-delimited token from a string into dst.
 */
static const char *extract_token(const char *src, char *dst, size_t dst_max) {
    while (*src == ' ' || *src == '\t') src++;
    if (*src == '\0' || *src == '\r' || *src == '\n') return NULL;

    size_t i = 0;
    while (*src && *src != ' ' && *src != '\t' && *src != '\r' && *src != '\n') {
        if (i + 1 < dst_max) {
            dst[i++] = *src;
        }
        src++;
    }
    dst[i] = '\0';
    return src;
}

/**
 * Handle incoming socket buffer:
 * Detects whether the request is HTTP (GET / POST) or native TCP wire protocol.
 */
static void handle_client_command(wheelmq_server_t *srv, const char *cmd_line, char *response, size_t resp_max) {
    // 1. Dual Protocol Check: If HTTP request, route via http_route()
    if (http_is_request(cmd_line, strlen(cmd_line))) {
        http_request_t hreq;
        if (http_parse(cmd_line, strlen(cmd_line), &hreq)) {
            http_route(srv, &hreq, response, resp_max);
            return;
        }
    }

    // 2. High-speed native TCP wire protocol
    char cmd[32];
    const char *p = extract_token(cmd_line, cmd, sizeof(cmd));
    if (!p) {
        snprintf(response, resp_max, "ERR empty_command\r\n");
        return;
    }

    pthread_mutex_lock(&srv->lock);

    if (strcasecmp(cmd, "PING") == 0) {
        snprintf(response, resp_max, "PONG\r\n");
    } else if (strcasecmp(cmd, "PUSH") == 0) {
        // Syntax: PUSH <queue> <delay_ms> [webhook_url] <payload...>
        char qname[64];
        char delay_str[32];

        p = extract_token(p, qname, sizeof(qname));
        if (p) p = extract_token(p, delay_str, sizeof(delay_str));

        if (!p) {
            snprintf(response, resp_max, "ERR syntax: PUSH <queue> <delay_ms> <payload>\r\n");
        } else {
            while (*p == ' ' || *p == '\t') p++;

            char webhook_url[256] = "";
            if (strncmp(p, "http://", 7) == 0 || strncmp(p, "https://", 8) == 0) {
                p = extract_token(p, webhook_url, sizeof(webhook_url));
                while (p && (*p == ' ' || *p == '\t')) p++;
            }

            size_t payload_len = p ? 0 : 0;
            if (p) {
                while (p[payload_len] && p[payload_len] != '\r' && p[payload_len] != '\n') {
                    payload_len++;
                }
            }

            uint64_t delay_ms = strtoull(delay_str, NULL, 10);
            uint64_t now = get_time_ms();
            uint64_t msg_id = ++srv->next_msg_id;
            uint64_t scheduled_at = now + delay_ms;

            message_t *msg = message_create(msg_id, qname, p, payload_len, scheduled_at, 3);
            if (strlen(webhook_url) > 0) {
                message_set_webhook(msg, webhook_url);
            }

            if (!msg) {
                snprintf(response, resp_max, "ERR out_of_memory\r\n");
            } else if (delay_ms > 0) {
                wheel_add(&srv->wheel, msg);
                snprintf(response, resp_max, "OK id=%llu status=scheduled delay_ms=%llu size=%zu\r\n",
                         (unsigned long long)msg_id, (unsigned long long)delay_ms, payload_len);
            } else {
                queue_t *q = server_find_or_create_queue(srv, qname);
                if (q) {
                    queue_enqueue_ready(q, msg);
                    snprintf(response, resp_max, "OK id=%llu status=ready size=%zu\r\n",
                             (unsigned long long)msg_id, payload_len);
                } else {
                    message_free(msg);
                    snprintf(response, resp_max, "ERR max_queues_reached\r\n");
                }
            }
        }
    } else if (strcasecmp(cmd, "POP") == 0) {
        char qname[64];
        p = extract_token(p, qname, sizeof(qname));
        if (!p) {
            snprintf(response, resp_max, "ERR syntax: POP <queue>\r\n");
        } else {
            queue_t *q = server_find_or_create_queue(srv, qname);
            message_t *msg = q ? queue_dequeue_ready(q) : NULL;
            if (msg) {
                snprintf(response, resp_max, "MESSAGE id=%llu retry=%u size=%zu payload=%.*s\r\n",
                         (unsigned long long)msg->id, msg->retry_count, msg->payload_len,
                         (int)msg->payload_len, (char *)msg->payload);
                message_free(msg);
            } else {
                snprintf(response, resp_max, "EMPTY\r\n");
            }
        }
    } else if (strcasecmp(cmd, "FAIL") == 0) {
        char qname[64];
        char id_str[32];
        p = extract_token(p, qname, sizeof(qname));
        if (p) p = extract_token(p, id_str, sizeof(id_str));

        if (!p) {
            snprintf(response, resp_max, "ERR syntax: FAIL <queue> <msg_id> [reason]\r\n");
        } else {
            queue_t *q = server_find_or_create_queue(srv, qname);
            uint64_t msg_id = strtoull(id_str, NULL, 10);

            while (*p == ' ' || *p == '\t') p++;
            char reason_buf[128] = "Execution failed";
            if (*p && *p != '\r' && *p != '\n') {
                size_t rlen = 0;
                while (p[rlen] && p[rlen] != '\r' && p[rlen] != '\n' && rlen < sizeof(reason_buf) - 1) {
                    reason_buf[rlen] = p[rlen];
                    rlen++;
                }
                reason_buf[rlen] = '\0';
            }

            message_t *msg = message_create(msg_id, qname, "failed_job_payload", 18, 0, 3);
            if (msg && q) {
                queue_move_to_dlq(q, msg, reason_buf);
                snprintf(response, resp_max, "OK id=%llu moved_to_dlq reason=\"%s\"\r\n",
                         (unsigned long long)msg_id, reason_buf);
            } else {
                snprintf(response, resp_max, "ERR cannot_fail_message\r\n");
            }
        }
    } else if (strcasecmp(cmd, "DLQ_LIST") == 0) {
        char qname[64];
        p = extract_token(p, qname, sizeof(qname));
        if (!p) {
            snprintf(response, resp_max, "ERR syntax: DLQ_LIST <queue>\r\n");
        } else {
            queue_t *q = server_find_or_create_queue(srv, qname);
            if (!q || queue_dlq_count(q) == 0) {
                snprintf(response, resp_max, "DLQ_COUNT 0\r\n");
            } else {
                size_t offset = snprintf(response, resp_max, "DLQ_COUNT %zu\r\n", queue_dlq_count(q));
                message_t *curr = q->dlq.head;
                while (curr && offset < resp_max - 256) {
                    offset += snprintf(response + offset, resp_max - offset,
                                       "  - id=%llu retries=%u reason=\"%s\"\r\n",
                                       (unsigned long long)curr->id, curr->retry_count, curr->error_msg);
                    curr = curr->next;
                }
            }
        }
    } else if (strcasecmp(cmd, "DLQ_REPLAY") == 0) {
        char qname[64];
        char id_str[32];
        p = extract_token(p, qname, sizeof(qname));
        if (p) p = extract_token(p, id_str, sizeof(id_str));

        if (!p) {
            snprintf(response, resp_max, "ERR syntax: DLQ_REPLAY <queue> <msg_id>\r\n");
        } else {
            queue_t *q = server_find_or_create_queue(srv, qname);
            uint64_t msg_id = strtoull(id_str, NULL, 10);
            if (q && queue_replay_dlq(q, msg_id)) {
                snprintf(response, resp_max, "OK replayed id=%llu\r\n", (unsigned long long)msg_id);
            } else {
                snprintf(response, resp_max, "ERR not_found_in_dlq\r\n");
            }
        }
    } else if (strcasecmp(cmd, "JOIN") == 0) {
        char peer_id[64];
        char peer_ip[64];
        char peer_port_str[16];
        p = extract_token(p, peer_id, sizeof(peer_id));
        if (p) p = extract_token(p, peer_ip, sizeof(peer_ip));
        if (p) p = extract_token(p, peer_port_str, sizeof(peer_port_str));

        if (!p) {
            snprintf(response, resp_max, "ERR syntax: JOIN <node_id> <ip> <port>\r\n");
        } else {
            uint16_t port = (uint16_t)atoi(peer_port_str);
            int idx = hashring_add_node(&srv->ring, peer_id, peer_ip, port);
            if (idx >= 0) {
                snprintf(response, resp_max, "OK peer_added id=%s nodes=%u vnodes=%u\r\n",
                         peer_id, srv->ring.node_count, srv->ring.vnode_count);
            } else {
                snprintf(response, resp_max, "ERR ring_full\r\n");
            }
        }
    } else if (strcasecmp(cmd, "RING_LOOKUP") == 0) {
        char key[64];
        p = extract_token(p, key, sizeof(key));
        if (!p) {
            snprintf(response, resp_max, "ERR syntax: RING_LOOKUP <key>\r\n");
        } else {
            const cluster_node_t *owner = hashring_lookup(&srv->ring, key, strlen(key));
            if (owner) {
                snprintf(response, resp_max, "OWNER node=%s addr=%s:%u is_local=%s\r\n",
                         owner->id, owner->address, owner->port,
                         (strcmp(owner->id, srv->node_id) == 0) ? "true" : "false");
            } else {
                snprintf(response, resp_max, "ERR no_nodes_in_ring\r\n");
            }
        }
    } else if (strcasecmp(cmd, "STATS") == 0) {
        size_t total_ready = 0;
        size_t total_dlq = 0;
        for (size_t i = 0; i < srv->queue_count; i++) {
            total_ready += queue_ready_count(&srv->queues[i]);
            total_dlq += queue_dlq_count(&srv->queues[i]);
        }
        snprintf(response, resp_max,
                 "NODE id=%s queues=%zu ready=%zu dlq=%zu timers=%zu ring_nodes=%u ring_vnodes=%u\r\n",
                 srv->node_id, srv->queue_count, total_ready, total_dlq,
                 srv->wheel.total_timer_count, srv->ring.node_count, srv->ring.vnode_count);
    } else {
        snprintf(response, resp_max, "ERR unknown_command\r\n");
    }

    pthread_mutex_unlock(&srv->lock);
}

int server_init(wheelmq_server_t *srv, const char *node_id, const char *bind_ip, uint16_t port, const char *unix_socket_path) {
    memset(srv, 0, sizeof(wheelmq_server_t));
    strncpy(srv->node_id, node_id, sizeof(srv->node_id) - 1);
    strncpy(srv->bind_ip, bind_ip, sizeof(srv->bind_ip) - 1);
    srv->port = port;

    if (unix_socket_path && strlen(unix_socket_path) > 0) {
        strncpy(srv->unix_socket_path, unix_socket_path, sizeof(srv->unix_socket_path) - 1);
    }

    pthread_mutex_init(&srv->lock, NULL);
    hashring_init(&srv->ring);

    hashring_add_node(&srv->ring, node_id, bind_ip, port);
    wheel_init(&srv->wheel, get_time_ms());

    srv->server_fd = -1;
    srv->running = false;
    return 0;
}

int server_run(wheelmq_server_t *srv) {
    if (strlen(srv->unix_socket_path) > 0) {
        unlink(srv->unix_socket_path);
        srv->server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (srv->server_fd < 0) {
            perror("socket(AF_UNIX)");
            return -1;
        }

        struct sockaddr_un un_addr;
        memset(&un_addr, 0, sizeof(un_addr));
        un_addr.sun_family = AF_UNIX;
        strncpy(un_addr.sun_path, srv->unix_socket_path, sizeof(un_addr.sun_path) - 1);

        if (bind(srv->server_fd, (struct sockaddr *)&un_addr, sizeof(un_addr)) < 0) {
            perror("bind(AF_UNIX)");
            close(srv->server_fd);
            return -1;
        }
    } else {
        srv->server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (srv->server_fd < 0) {
            perror("socket(AF_INET)");
            return -1;
        }

        int opt = 1;
        setsockopt(srv->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(srv->port);
        inet_pton(AF_INET, srv->bind_ip, &addr.sin_addr);

        if (bind(srv->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("bind(AF_INET)");
            close(srv->server_fd);
            return -1;
        }
    }

    if (listen(srv->server_fd, 1024) < 0) {
        perror("listen");
        close(srv->server_fd);
        return -1;
    }

    srv->running = true;

    if (pthread_create(&srv->ticker_thread, NULL, ticker_routine, srv) != 0) {
        perror("pthread_create");
        close(srv->server_fd);
        return -1;
    }

    if (strlen(srv->unix_socket_path) > 0) {
        printf("[wheelmq] Node '%s' listening on UNIX socket '%s'\n",
               srv->node_id, srv->unix_socket_path);
    } else {
        printf("[wheelmq] Node '%s' listening on TCP %s:%u (REST API + TCP Wire Protocol Active)\n",
               srv->node_id, srv->bind_ip, srv->port);
    }

    char *read_buf = (char *)malloc(SERVER_IO_BUF_SIZE);
    char *resp_buf = (char *)malloc(SERVER_IO_BUF_SIZE);

    if (!read_buf || !resp_buf) {
        fprintf(stderr, "Failed to allocate IO buffers\n");
        return -1;
    }

    while (srv->running) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(srv->server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!srv->running) break;
            continue;
        }

        ssize_t n = read(client_fd, read_buf, SERVER_IO_BUF_SIZE - 1);
        if (n > 0) {
            read_buf[n] = '\0';
            handle_client_command(srv, read_buf, resp_buf, SERVER_IO_BUF_SIZE);
            write(client_fd, resp_buf, strlen(resp_buf));
        }
        close(client_fd);
    }

    free(read_buf);
    free(resp_buf);
    return 0;
}

void server_stop(wheelmq_server_t *srv) {
    if (srv->running) {
        srv->running = false;
        if (srv->server_fd >= 0) {
            shutdown(srv->server_fd, SHUT_RDWR);
            close(srv->server_fd);
            srv->server_fd = -1;
        }
        if (strlen(srv->unix_socket_path) > 0) {
            unlink(srv->unix_socket_path);
        }
        pthread_join(srv->ticker_thread, NULL);
    }
}

void server_destroy(wheelmq_server_t *srv) {
    server_stop(srv);
    pthread_mutex_lock(&srv->lock);
    for (size_t i = 0; i < srv->queue_count; i++) {
        queue_destroy(&srv->queues[i]);
    }
    wheel_destroy(&srv->wheel);
    pthread_mutex_unlock(&srv->lock);
    pthread_mutex_destroy(&srv->lock);
}
