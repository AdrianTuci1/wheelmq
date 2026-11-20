#!/usr/bin/env bash
set -e

# ==============================================================================
# wheelmq Linux Core-Infrastructure Curl Installer
# Fetches pre-compiled static Linux binaries directly via curl. Zero build tools.
# ==============================================================================

REPO="AdrianTuci1/wheelmq"
INSTALL_DIR="${INSTALL_DIR:-/usr/local/bin}"

echo "================================================================"
echo "          Installing wheelmq for Linux via curl..."
echo "================================================================"

# 1. Verify operating system is Linux
OS=$(uname -s | tr '[:upper:]' '[:lower:]')
if [ "$OS" != "linux" ]; then
    echo "ERROR: This binary installer target is strictly for Linux."
    echo "Detected OS: $OS"
    echo "To compile locally on macOS/BSD, clone the repo and run 'make all'."
    exit 1
fi

# 2. Detect machine architecture
ARCH=$(uname -m)
case "$ARCH" in
    x86_64|amd64)
        BIN_ARCH="amd64"
        ;;
    aarch64|arm64)
        BIN_ARCH="arm64"
        ;;
    *)
        echo "ERROR: Unsupported Linux architecture: $ARCH"
        exit 1
        ;;
esac

echo "Detected Linux architecture: $BIN_ARCH"

# 3. Download URLs for static Linux binaries
RELEASE_URL="https://github.com/${REPO}/releases/latest/download"
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

echo "Downloading wheelmq-linux-${BIN_ARCH} via curl..."
if curl -fsSL -o "${TMP_DIR}/wheelmq" "${RELEASE_URL}/wheelmq-linux-${BIN_ARCH}"; then
    curl -fsSL -o "${TMP_DIR}/wheelmq-cli" "${RELEASE_URL}/wheelmq-cli-linux-${BIN_ARCH}" || true
    chmod +x "${TMP_DIR}/wheelmq"
    if [ -f "${TMP_DIR}/wheelmq-cli" ]; then
        chmod +x "${TMP_DIR}/wheelmq-cli"
    fi
else
    echo "Notice: Pre-built release binary not found online. Falling back to local make build..."
    if command -v make >/dev/null 2>&1 && (command -v gcc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1); then
        git clone --depth 1 "https://github.com/${REPO}.git" "${TMP_DIR}/src"
        cd "${TMP_DIR}/src" && make all
        cp bin/wheelmq "${TMP_DIR}/wheelmq"
        cp bin/wheelmq-cli "${TMP_DIR}/wheelmq-cli"
    else
        echo "ERROR: Could not fetch pre-compiled binary and no C compiler found on host."
        exit 1
    fi
fi

# 4. Install binaries
if [ -w "$INSTALL_DIR" ]; then
    mv "${TMP_DIR}/wheelmq" "${INSTALL_DIR}/wheelmq"
    if [ -f "${TMP_DIR}/wheelmq-cli" ]; then
        mv "${TMP_DIR}/wheelmq-cli" "${INSTALL_DIR}/wheelmq-cli"
    fi
else
    echo "Elevating privileges to install into ${INSTALL_DIR}..."
    sudo mv "${TMP_DIR}/wheelmq" "${INSTALL_DIR}/wheelmq"
    if [ -f "${TMP_DIR}/wheelmq-cli" ]; then
        sudo mv "${TMP_DIR}/wheelmq-cli" "${INSTALL_DIR}/wheelmq-cli"
    fi
fi

echo "================================================================"
echo "✓ wheelmq successfully installed into ${INSTALL_DIR}/wheelmq"
echo ""
echo "Verify installation:"
echo "  wheelmq --help"
echo "Start node:"
echo "  wheelmq --id node-1 --port 7000"
echo "================================================================"
