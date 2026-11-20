#!/usr/bin/env bash
set -e

# wheelmq fast installer for Linux & macOS
INSTALL_DIR="/usr/local/bin"

echo "=========================================================="
echo "          Installing wheelmq from source..."
echo "=========================================================="

TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT

git clone --depth 1 https://github.com/AdrianTuci1/wheelmq.git "$TEMP_DIR/wheelmq"
cd "$TEMP_DIR/wheelmq"

make all

if [ -w "$INSTALL_DIR" ]; then
    cp bin/wheelmq "$INSTALL_DIR/wheelmq"
    cp bin/wheelmq-cli "$INSTALL_DIR/wheelmq-cli"
else
    echo "Elevating privileges to install into $INSTALL_DIR..."
    sudo cp bin/wheelmq "$INSTALL_DIR/wheelmq"
    sudo cp bin/wheelmq-cli "$INSTALL_DIR/wheelmq-cli"
fi

echo "=========================================================="
echo "✓ wheelmq successfully installed to $INSTALL_DIR/wheelmq"
echo "✓ wheelmq-cli successfully installed to $INSTALL_DIR/wheelmq-cli"
echo ""
echo "Start your node with:"
echo "  wheelmq --id node-1 --port 7000"
echo "=========================================================="
