#!/bin/bash
# Bolt Agent Installer
# Usage: curl -fsSL https://raw.githubusercontent.com/General-zzz-trade/Bolt/master/install.sh | bash
set -e

REPO="General-zzz-trade/Bolt"
INSTALL_DIR="/usr/local/bin"

# Auto-detect latest release version
VERSION=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" 2>/dev/null | grep '"tag_name"' | head -1 | sed 's/.*"v\([^"]*\)".*/\1/')
if [ -z "$VERSION" ]; then
    VERSION="0.6.0"  # Fallback
fi

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
DIM='\033[2m'
BOLD='\033[1m'
RESET='\033[0m'

echo -e "\n${BOLD}${CYAN}⚡ Bolt Agent Installer${RESET} v${VERSION}\n"

# Detect platform
OS="$(uname -s)"
ARCH="$(uname -m)"

case "${OS}" in
    Linux*)   PLATFORM="linux" ;;
    Darwin*)  PLATFORM="macos" ;;
    *)        echo -e "${RED}Unsupported OS: ${OS}${RESET}"; exit 1 ;;
esac

case "${ARCH}" in
    x86_64|amd64)  ARCH_NAME="x64" ;;
    aarch64|arm64) ARCH_NAME="arm64" ;;
    *)             echo -e "${RED}Unsupported architecture: ${ARCH}${RESET}"; exit 1 ;;
esac

BINARY_NAME="bolt-${PLATFORM}-${ARCH_NAME}"
if [ "${PLATFORM}" = "macos" ] && [ "${ARCH_NAME}" = "x64" ]; then
    BINARY_NAME="bolt-macos-arm64"  # macOS builds are universal/arm64
fi

DOWNLOAD_URL="https://github.com/${REPO}/releases/download/v${VERSION}/${BINARY_NAME}"

echo -e "  Platform:  ${BOLD}${OS} ${ARCH}${RESET}"
echo -e "  Binary:    ${DIM}${BINARY_NAME}${RESET}"
echo -e "  Install:   ${DIM}${INSTALL_DIR}/bolt${RESET}"
echo ""

# Check if already installed
if command -v bolt &>/dev/null; then
    CURRENT=$(bolt -v 2>/dev/null || echo "unknown")
    echo -e "  ${DIM}Existing installation found: ${CURRENT}${RESET}"
fi

# Download
echo -e "  ${CYAN}Downloading...${RESET}"
TMP_FILE=$(mktemp)
if command -v curl &>/dev/null; then
    HTTP_CODE=$(curl -fsSL -w "%{http_code}" -o "${TMP_FILE}" "${DOWNLOAD_URL}" 2>/dev/null || true)
elif command -v wget &>/dev/null; then
    wget -q -O "${TMP_FILE}" "${DOWNLOAD_URL}" 2>/dev/null && HTTP_CODE="200" || HTTP_CODE="404"
else
    echo -e "${RED}Error: curl or wget required${RESET}"
    rm -f "${TMP_FILE}"
    exit 1
fi

if [ "${HTTP_CODE}" != "200" ] || [ ! -s "${TMP_FILE}" ]; then
    rm -f "${TMP_FILE}"
    echo -e "${RED}Download failed (HTTP ${HTTP_CODE})${RESET}"
    echo -e "${DIM}URL: ${DOWNLOAD_URL}${RESET}"
    echo -e "\nAlternative install methods:"
    echo -e "  npm install -g bolt-agent"
    echo -e "  Build from source: https://github.com/${REPO}"
    exit 1
fi

# Install
chmod +x "${TMP_FILE}"

if [ -w "${INSTALL_DIR}" ]; then
    mv "${TMP_FILE}" "${INSTALL_DIR}/bolt"
else
    echo -e "  ${DIM}Requires sudo for ${INSTALL_DIR}${RESET}"
    sudo mv "${TMP_FILE}" "${INSTALL_DIR}/bolt"
fi

# Verify
if command -v bolt &>/dev/null; then
    echo -e "\n  ${GREEN}✓ Installed successfully!${RESET}\n"
    bolt -v 2>/dev/null || true
    echo -e "\n  Get started:"
    echo -e "    ${BOLD}bolt${RESET}                  Interactive mode (setup wizard on first run)"
    echo -e "    ${BOLD}bolt doctor${RESET}            Check environment"
    echo -e "    ${BOLD}bolt --help${RESET}            Show all commands"
    echo -e "\n  ${DIM}Docs: https://github.com/${REPO}${RESET}\n"
else
    echo -e "\n  ${GREEN}✓ Downloaded to ${INSTALL_DIR}/bolt${RESET}"
    echo -e "  ${DIM}Add ${INSTALL_DIR} to your PATH if needed${RESET}\n"
fi
