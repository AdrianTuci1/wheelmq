CC ?= clang
CFLAGS = -Wall -Wextra -Werror -O3 -std=c11 -Iinclude -pthread
BIN_DIR = bin
TEST_DIR = tests
SRC_DIR = src
TOOLS_DIR = tools

LIB_SRCS = $(SRC_DIR)/hashring.c $(SRC_DIR)/queue.c $(SRC_DIR)/wheel.c $(SRC_DIR)/webhook.c $(SRC_DIR)/http.c $(SRC_DIR)/server.c
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BIN_DIR)/%.o, $(LIB_SRCS))

TARGET = $(BIN_DIR)/wheelmq
CLI_TARGET = $(BIN_DIR)/wheelmq-cli
TEST_BINS = $(BIN_DIR)/test_hashring $(BIN_DIR)/test_queue $(BIN_DIR)/test_wheel $(BIN_DIR)/test_server

all: $(TARGET) $(CLI_TARGET) $(TEST_BINS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/%.o: $(SRC_DIR)/%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(SRC_DIR)/main.c $(LIB_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@

$(CLI_TARGET): $(TOOLS_DIR)/cli.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(BIN_DIR)/test_hashring: $(TEST_DIR)/test_hashring.c $(LIB_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@

$(BIN_DIR)/test_queue: $(TEST_DIR)/test_queue.c $(LIB_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@

$(BIN_DIR)/test_wheel: $(TEST_DIR)/test_wheel.c $(LIB_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@

$(BIN_DIR)/test_server: $(TEST_DIR)/test_server.c $(LIB_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@

test: $(TEST_BINS)
	@echo "=========================================="
	@echo "Running wheelmq test suite..."
	@echo "=========================================="
	./$(BIN_DIR)/test_hashring
	@echo ""
	./$(BIN_DIR)/test_queue
	@echo ""
	./$(BIN_DIR)/test_wheel
	@echo ""
	./$(BIN_DIR)/test_server
	@echo "=========================================="
	@echo "All tests passed successfully!"
	@echo "=========================================="

clean:
	rm -rf $(BIN_DIR)

.PHONY: all test clean
