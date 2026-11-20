#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define CLI_BUF_SIZE 65536 // 64 KB

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s [options] <COMMAND> [args...]\n", argv[0]);
        printf("Options:\n");
        printf("  -h <host>    TCP host (default: 127.0.0.1)\n");
        printf("  -p <port>    TCP port (default: 7000)\n");
        printf("  -s <path>    UNIX socket path (e.g. /tmp/wheelmq.sock)\n\n");
        printf("Commands:\n");
        printf("  push <queue> <delay_ms> <payload>  (Enqueue immediate or delayed task)\n");
        printf("  pop <queue>                        (Dequeue next ready FIFO task)\n");
        printf("  fail <queue> <msg_id> [reason]     (Move failed task to DLQ)\n");
        printf("  dlq_list <queue>                   (List tasks in Dead Letter Queue)\n");
        printf("  dlq_replay <queue> <msg_id>        (Replay task from DLQ back to FIFO)\n");
        printf("  stats                              (Cluster and queue statistics)\n");
        printf("  ring_lookup <key>                  (Find cluster node owning key)\n");
        printf("  join <id> <ip> <port>              (Join a cluster peer to hash ring)\n");
        return 1;
    }

    char host[64] = "127.0.0.1";
    int port = 7000;
    char unix_socket[128] = "";
    int cmd_start = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            strncpy(host, argv[++i], sizeof(host) - 1);
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            strncpy(unix_socket, argv[++i], sizeof(unix_socket) - 1);
            cmd_start = i + 1;
        } else {
            break;
        }
    }

    char *cmd_buf = (char *)malloc(CLI_BUF_SIZE);
    char *resp_buf = (char *)malloc(CLI_BUF_SIZE);
    if (!cmd_buf || !resp_buf) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    cmd_buf[0] = '\0';

    for (int i = cmd_start; i < argc; i++) {
        strncat(cmd_buf, argv[i], CLI_BUF_SIZE - strlen(cmd_buf) - 2);
        if (i < argc - 1) {
            strncat(cmd_buf, " ", CLI_BUF_SIZE - strlen(cmd_buf) - 2);
        }
    }
    strncat(cmd_buf, "\r\n", CLI_BUF_SIZE - strlen(cmd_buf) - 1);

    int sock = -1;
    if (strlen(unix_socket) > 0) {
        sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("socket(AF_UNIX)");
            free(cmd_buf);
            free(resp_buf);
            return 1;
        }
        struct sockaddr_un un_addr;
        memset(&un_addr, 0, sizeof(un_addr));
        un_addr.sun_family = AF_UNIX;
        size_t path_len = strlen(unix_socket);
        if (path_len >= sizeof(un_addr.sun_path)) {
            path_len = sizeof(un_addr.sun_path) - 1;
        }
        memcpy(un_addr.sun_path, unix_socket, path_len);
        un_addr.sun_path[path_len] = '\0';

        if (connect(sock, (struct sockaddr *)&un_addr, sizeof(un_addr)) < 0) {
            perror("connect(AF_UNIX)");
            close(sock);
            free(cmd_buf);
            free(resp_buf);
            return 1;
        }
    } else {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("socket(AF_INET)");
            free(cmd_buf);
            free(resp_buf);
            return 1;
        }
        struct sockaddr_in srv_addr;
        memset(&srv_addr, 0, sizeof(srv_addr));
        srv_addr.sin_family = AF_INET;
        srv_addr.sin_port = htons(port);
        inet_pton(AF_INET, host, &srv_addr.sin_addr);

        if (connect(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
            perror("connect(AF_INET)");
            close(sock);
            free(cmd_buf);
            free(resp_buf);
            return 1;
        }
    }

    ssize_t w = write(sock, cmd_buf, strlen(cmd_buf));
    (void)w;

    ssize_t n = read(sock, resp_buf, CLI_BUF_SIZE - 1);
    if (n > 0) {
        resp_buf[n] = '\0';
        printf("%s", resp_buf);
    }

    close(sock);
    free(cmd_buf);
    free(resp_buf);
    return 0;
}
