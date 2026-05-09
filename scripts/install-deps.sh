#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "--help" ]]; then
    cat <<'EOF'
Usage: scripts/install-deps.sh

Ubuntu/Debian bootstrap for cuddlePanel.

What it does:
  - installs core build dependencies
  - installs common runtime dependencies used by cuddlePanel features
  - creates basic runtime directories
  - creates .env from repo defaults if one does not already exist

What it does not do:
  - create the first superadmin
  - configure nginx sites
  - install a deploy-site helper
EOF
    exit 0
fi

if [[ "${EUID}" -ne 0 ]]; then
    echo "Please run as root or via sudo." >&2
    exit 1
fi

if ! command -v apt-get >/dev/null 2>&1; then
    echo "This bootstrap script currently supports Ubuntu/Debian hosts with apt-get." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

export DEBIAN_FRONTEND=noninteractive

APT_PACKAGES=(
    adduser
    bash
    build-essential
    cmake
    coreutils
    curl
    g++
    git
    libsodium-dev
    libssl-dev
    nginx
    pkg-config
    passwd
)

UPX_PACKAGE=""
if apt-cache show upx >/dev/null 2>&1; then
    UPX_PACKAGE="upx"
elif apt-cache show upx-ucl >/dev/null 2>&1; then
    UPX_PACKAGE="upx-ucl"
fi

echo "Updating apt package metadata..."
apt-get update

if [[ -n "${UPX_PACKAGE}" ]]; then
    APT_PACKAGES+=("${UPX_PACKAGE}")
fi

echo "Installing cuddlePanel host dependencies..."
apt-get install -y "${APT_PACKAGES[@]}"

echo "Preparing runtime directories..."
mkdir -p "${REPO_ROOT}/data" "${REPO_ROOT}/bin" "${REPO_ROOT}/build"
chmod 700 "${REPO_ROOT}/data"

if [[ ! -f "${REPO_ROOT}/.env" ]]; then
    echo "Seeding .env from repo defaults..."
    cat > "${REPO_ROOT}/.env" <<'EOF'
# cuddlePanel runtime environment
# This file is auto-loaded by the server from the current working directory when present.
# Existing shell-exported environment variables take precedence over values here.

# Network and cookies
CUDDLEPANEL_PORT=1337

# A random 32-byte hex string used to sign session cookies. You can generate one with: openssl rand -hex 32
CUDDLEPANEL_SECURE_COOKIES=

# Deploy helper
CUDDLEPANEL_DEPLOY_SITE_BIN=/usr/local/sbin/deploy-site

# Nginx management
CUDDLEPANEL_NGINX_AVAILABLE_DIR=/etc/nginx/sites-available
CUDDLEPANEL_NGINX_ENABLED_DIR=/etc/nginx/sites-enabled
CUDDLEPANEL_NGINX_BIN=/usr/sbin/nginx
CUDDLEPANEL_NGINX_RELOAD_SERVICE=nginx

# System administration data sources
CUDDLEPANEL_PASSWD_FILE=/etc/passwd
CUDDLEPANEL_GROUP_FILE=/etc/group
CUDDLEPANEL_SHADOW_FILE=/etc/shadow

# System administration command paths
CUDDLEPANEL_USERADD_BIN=/usr/sbin/useradd
CUDDLEPANEL_PASSWD_BIN=/usr/bin/passwd
CUDDLEPANEL_USERMOD_BIN=/usr/sbin/usermod
CUDDLEPANEL_GPASSWD_BIN=/usr/bin/gpasswd
CUDDLEPANEL_CHOWN_BIN=/bin/chown
CUDDLEPANEL_CHMOD_BIN=/bin/chmod

# Constrained roots for chown/chmod actions
CUDDLEPANEL_SYSTEM_ALLOWED_ROOTS=/home,/srv,/var/www

# Terminal runtime policy
CUDDLEPANEL_TERMINAL_SHELL=/bin/bash
CUDDLEPANEL_TERMINAL_RUN_AS_USER=nobody
CUDDLEPANEL_TERMINAL_RUN_AS_GROUP=nogroup
CUDDLEPANEL_TERMINAL_WORKDIR=/tmp
CUDDLEPANEL_TERMINAL_MAX_SESSIONS_PER_USER=2
CUDDLEPANEL_TERMINAL_IDLE_TIMEOUT_SECONDS=900
CUDDLEPANEL_TERMINAL_MAX_SESSION_SECONDS=7200
EOF
fi

echo
echo "Bootstrap complete."
echo "Next steps:"
echo "  1. Review ${REPO_ROOT}/.env"
echo "  2. Run: make build"
echo "  3. Start the server and complete first-run setup in the browser"
