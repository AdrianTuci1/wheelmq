#include "hashring.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("[TEST] Consistent Hash Ring...\n");

    hashring_t ring;
    hashring_init(&ring);

    // 1. Single node: must own 100% of keys
    int idx1 = hashring_add_node(&ring, "node-1", "127.0.0.1", 9001);
    assert(idx1 == 0);
    assert(ring.node_count == 1);
    assert(ring.vnode_count == HASHRING_VNODES_PER_NODE);

    for (int i = 0; i < 100; i++) {
        char key[32];
        snprintf(key, sizeof(key), "order-queue-%d", i);
        const cluster_node_t *node = hashring_lookup(&ring, key, strlen(key));
        assert(node != NULL);
        assert(strcmp(node->id, "node-1") == 0);
    }
    printf("  [PASS] Single node owns 100%% of keys\n");

    // 2. Add 2 more nodes (total 3 nodes)
    hashring_add_node(&ring, "node-2", "127.0.0.1", 9002);
    hashring_add_node(&ring, "node-3", "127.0.0.1", 9003);
    assert(ring.node_count == 3);
    assert(ring.vnode_count == 3 * HASHRING_VNODES_PER_NODE);

    // Check key distribution across 3 nodes for 10,000 keys
    int counts[3] = {0, 0, 0};
    for (int i = 0; i < 10000; i++) {
        char key[32];
        snprintf(key, sizeof(key), "task-key-%d", i);
        const cluster_node_t *node = hashring_lookup(&ring, key, strlen(key));
        assert(node != NULL);
        if (strcmp(node->id, "node-1") == 0) counts[0]++;
        else if (strcmp(node->id, "node-2") == 0) counts[1]++;
        else if (strcmp(node->id, "node-3") == 0) counts[2]++;
    }

    printf("  Distribution across 3 nodes for 10,000 keys:\n");
    printf("    node-1: %d (%.2f%%)\n", counts[0], counts[0] / 100.0);
    printf("    node-2: %d (%.2f%%)\n", counts[1], counts[1] / 100.0);
    printf("    node-3: %d (%.2f%%)\n", counts[2], counts[2] / 100.0);

    // Ensure roughly balanced (each node has between 25% and 40%)
    assert(counts[0] > 2500 && counts[0] < 4200);
    assert(counts[1] > 2500 && counts[1] < 4200);
    assert(counts[2] > 2500 && counts[2] < 4200);
    printf("  [PASS] Balanced distribution verified\n");

    // 3. Mark node-2 as dead (Failover test)
    hashring_set_node_status(&ring, "node-2", false);
    for (int i = 0; i < 1000; i++) {
        char key[32];
        snprintf(key, sizeof(key), "failover-key-%d", i);
        const cluster_node_t *node = hashring_lookup(&ring, key, strlen(key));
        assert(node != NULL);
        // node-2 must never be selected while dead
        assert(strcmp(node->id, "node-2") != 0);
    }
    printf("  [PASS] Node failure handled: zero traffic routed to dead node\n");

    printf("[SUCCESS] All Hash Ring tests passed!\n");
    return 0;
}
