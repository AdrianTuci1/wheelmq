#define _POSIX_C_SOURCE 200809L

#include "webhook.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/**
 * Parse an HTTP URL into host, port, and path components.
 */
static bool parse_http_url(const char *url, char *host, size_t host_max, uint16_t *port, char *path, size_t path_max) {
    if (!url) return false;

    const char *prefix = "http://";
    size_t prefix_len = strlen(prefix);
    if (strncasecmp(url, prefix, prefix_len) != 0) {
        return false; // Currently supporting HTTP
    }

    const char *p = url + prefix_len;
    const char *slash = strchr(p, '/');

    // Extract path
    if (slash) {
        strncpy(path, slash, path_max - 1);
        path[path_max - 1] = '\0';
    } else {
        strncpy(path, "/", path_max - 1);
        path[path_max - 1] = '\0';
    }

    // Extract host and optional port
    char host_port[128];
    size_t hp_len = slash ? (size_t)(slash - p) : strlen(p);
    if (hp_len >= sizeof(host_port)) hp_len = sizeof(host_port) - 1;
    memcpy(host_port, p, hp_len);
    host_port[hp_len] = '\0';

    char *colon = strchr(host_port, ':');
    if (colon) {
        *colon = '\0';
        *port = (uint16_t)atoi(colon + 1);
    } else {
        *port = 80;
    }

    strncpy(host, host_port, host_max - 1);
    host[host_max - 1] = '\0';
    return true;
}

webhook_result_t webhook_dispatch(const char *url, const void *payload, size_t payload_len, uint32_t timeout_ms) {
    webhook_result_t res;
    memset(&res, 0, sizeof(res));
    res.http_status = -1;

    char host[128];
    char path[256];
    uint16_t port = 80;

    if (!parse_http_url(url, host, sizeof(host), &port, path, sizeof(path))) {
        snprintf(res.error_desc, sizeof(res.error_desc), "Malformed URL or unsupported scheme");
        return res;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        snprintf(res.error_desc, sizeof(res.error_desc), "Failed to create socket");
        return res;
    }

    // Set socket timeouts
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &srv_addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(host);
        if (!he || !he->h_addr_list[0]) {
            snprintf(res.error_desc, sizeof(res.error_desc), "DNS resolution failed for '%s'", host);
            close(sock);
            return res;
        }
        memcpy(&srv_addr.sin_addr, he->h_addr_list[0], sizeof(srv_addr.sin_addr));
    }

    if (connect(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        snprintf(res.error_desc, sizeof(res.error_desc), "Connection refused to %s:%u", host, port);
        close(sock);
        return res;
    }

    // Construct HTTP/1.1 POST request
    char req_header[1024];
    int header_len = snprintf(req_header, sizeof(req_header),
             "POST %s HTTP/1.1\r\n"
             "Host: %s:%u\r\n"
             "User-Agent: wheelmq-webhook/1.0\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n",
             path, host, port, payload_len);

    if (write(sock, req_header, header_len) < 0 ||
        (payload && payload_len > 0 && write(sock, payload, payload_len) < 0)) {
        snprintf(res.error_desc, sizeof(res.error_desc), "Write failed to %s", host);
        close(sock);
        return res;
    }

    // Read HTTP response status
    char resp_buf[512];
    ssize_t n = read(sock, resp_buf, sizeof(resp_buf) - 1);
    close(sock);

    if (n <= 0) {
        snprintf(res.error_desc, sizeof(res.error_desc), "No response received (timeout)");
        return res;
    }

    resp_buf[n] = '\0';
    int status = 0;
    if (sscanf(resp_buf, "HTTP/1.%*d %d", &status) == 1) {
        res.http_status = status;
        if (status >= 200 && status < 300) {
            res.success = true;
        } else {
            snprintf(res.error_desc, sizeof(res.error_desc), "HTTP returned status %d", status);
        }
    } else {
        snprintf(res.error_desc, sizeof(res.error_desc), "Malformed HTTP response header");
    }

    return res;
}
