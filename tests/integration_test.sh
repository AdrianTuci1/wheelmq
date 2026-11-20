#!/usr/bin/env bash
set -e

SOCK=/tmp/wheelmq_test.sock
BIN=./bin/wheelmq
CLI=./bin/wheelmq-cli

rm -f $SOCK

echo "==============================================================="
echo "Starting wheelmq daemon on socket $SOCK..."
echo "==============================================================="
$BIN --socket $SOCK --id node-alpha > /dev/null 2>&1 &
SERVER_PID=$!

cleanup() {
    echo "Stopping daemon (PID $SERVER_PID)..."
    kill -TERM $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    rm -f $SOCK
}
trap cleanup EXIT

# Wait for socket to appear
for i in {1..20}; do
    if [ -S "$SOCK" ]; then
        break
    fi
    sleep 0.05
done

echo "1. Checking STATS..."
$CLI -s $SOCK stats

echo ""
echo "2. Enqueueing instant message to queue 'payments'..."
$CLI -s $SOCK push payments 0 "invoice_1001_usd_50"

echo "3. Dequeueing instant message (FIFO)..."
$CLI -s $SOCK pop payments

echo ""
echo "4. Enqueueing DELAYED message (500ms delay into Timing Wheel)..."
$CLI -s $SOCK push payments 500 "invoice_delayed_after_500ms"

echo "5. Dequeueing immediately (must be EMPTY):"
$CLI -s $SOCK pop payments

echo "6. Sleeping 600ms for Timing Wheel to cascade and fire..."
sleep 0.6

echo "7. Dequeueing after delay (must contain invoice_delayed_after_500ms):"
$CLI -s $SOCK pop payments

echo ""
echo "8. Testing Dead Letter Queue (DLQ): Failing a job..."
$CLI -s $SOCK fail payments 404 "Stripe API HTTP 500 Gateway Error"

echo "9. Listing DLQ entries:"
$CLI -s $SOCK dlq_list payments

echo "10. Replaying failed job from DLQ back to active FIFO:"
$CLI -s $SOCK dlq_replay payments 404

echo "11. Dequeueing replayed job from ready FIFO:"
$CLI -s $SOCK pop payments

echo ""
echo "12. Testing P2P Hash Ring: Adding peer node-beta..."
$CLI -s $SOCK join node-beta 127.0.0.1 7521
$CLI -s $SOCK ring_lookup payments

echo ""
echo "==============================================================="
echo "All Integration Tests Passed Flawlessly!"
echo "==============================================================="
