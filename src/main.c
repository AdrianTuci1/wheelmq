#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static wheelmq_server_t g_server;

static void handle_sigint(int sig) {
    (void)sig;
    printf("\n[wheelmq] Shutting down gracefully...\n");
    server_stop(&g_server);
}

static void print_banner(void) {
    printf("===============================================================\n");
    printf("       W H E E L M Q  -  Distributed P2P Timing & Queue Engine \n");
    printf("       Written in pure C11 | Zero Dependencies | O(1) Sched   \n");
    printf("===============================================================\n");
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --id <node_id>       Unique cluster node identifier (default: node-1)\n");
    printf("  --bind <ip>          Bind IP address (default: 127.0.0.1)\n");
    printf("  --port <port>        TCP listen port (default: 7000)\n");
    printf("  --socket <path>      Bind to UNIX domain socket (e.g. /tmp/wheelmq.sock)\n");
    printf("  --peer <id,ip,port>  Join an initial cluster peer (e.g. node-2,127.0.0.1,7001)\n");
    printf("  --help               Display this help message\n\n");
}

int main(int argc, char *argv[]) {
    char node_id[64] = "node-1";
    char bind_ip[64] = "127.0.0.1";
    uint16_t port = 7000;
    char unix_socket[128] = "";
    char peer_arg[128] = "";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            strncpy(node_id, argv[++i], sizeof(node_id) - 1);
        } else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
            strncpy(bind_ip, argv[++i], sizeof(bind_ip) - 1);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            strncpy(unix_socket, argv[++i], sizeof(unix_socket) - 1);
        } else if (strcmp(argv[i], "--peer") == 0 && i + 1 < argc) {
            strncpy(peer_arg, argv[++i], sizeof(peer_arg) - 1);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    print_banner();

    if (server_init(&g_server, node_id, bind_ip, port, unix_socket) != 0) {
        fprintf(stderr, "Failed to initialize server\n");
        return 1;
    }

    if (strlen(peer_arg) > 0) {
        char p_id[64], p_ip[64];
        int p_port = 0;
        if (sscanf(peer_arg, "%63[^,],%63[^,],%d", p_id, p_ip, &p_port) == 3) {
            hashring_add_node(&g_server.ring, p_id, p_ip, (uint16_t)p_port);
            printf("[wheelmq] Added initial peer: %s at %s:%d\n", p_id, p_ip, p_port);
        } else {
            fprintf(stderr, "[wheelmq] Warning: Invalid peer format. Expected <id,ip,port>\n");
        }
    }

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    server_run(&g_server);
    server_destroy(&g_server);

    printf("[wheelmq] Stopped.\n");
    return 0;
}
