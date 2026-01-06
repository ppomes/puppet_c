#!/bin/bash
# Download Puppet Forge modules for demo
# These are the minimum modules needed for the demo

set -e

# Default to demo/modules which is mounted in the container
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULES_DIR="${1:-$SCRIPT_DIR/modules}"
mkdir -p "$MODULES_DIR"

echo "Downloading Puppet modules to $MODULES_DIR..."

# Function to download and extract a module from Puppet Forge
download_module() {
    local author=$1
    local name=$2
    local version=$3

    local url="https://forgeapi.puppet.com/v3/files/${author}-${name}-${version}.tar.gz"
    local tarball="${author}-${name}-${version}.tar.gz"

    echo "  - ${author}/${name} (${version})"

    if [ -d "$MODULES_DIR/$name" ]; then
        echo "    (already exists, skipping)"
        return
    fi

    curl -sLo "/tmp/$tarball" "$url"
    tar -xzf "/tmp/$tarball" -C "$MODULES_DIR"
    mv "$MODULES_DIR/${author}-${name}-${version}" "$MODULES_DIR/$name"
    rm -f "/tmp/$tarball"
}

# Core modules
download_module "puppetlabs" "stdlib" "9.7.0"
download_module "puppetlabs" "mysql" "16.3.0"

echo "Done! Modules installed in $MODULES_DIR"
echo ""
echo "To use in demo:"
echo "  docker compose -f docker-compose.demo.yml up -d"
