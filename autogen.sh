#!/bin/bash

# Bootstrap script for autotools build system

set -e

echo "Bootstrapping autotools build system..."

# Check for required tools
check_tool() {
    if ! command -v "$1" &> /dev/null; then
        echo "Error: $1 is required but not found"
        echo "Please install autotools: brew install autoconf automake (macOS) or apt-get install autotools-dev (Linux)"
        exit 1
    fi
}

check_tool "autoreconf"
check_tool "autoconf"
check_tool "automake"

# Generate build system
echo "Running autoreconf..."
autoreconf --install --verbose --force

echo ""
echo "Bootstrap complete!"
echo "Now run:"
echo "  ./configure [options]"
echo "  make"
echo "  make check"
echo ""
echo "Available configure options:"
echo "  --with-treesitter=PATH   Specify tree-sitter installation path"
echo "  --with-ruby=PATH         Specify Ruby installation path"
echo "  --with-yaml=PATH         Specify libyaml installation path"
echo "  --with-openssl=PATH      Specify OpenSSL installation path"
echo "  --with-microhttpd=PATH   Specify libmicrohttpd installation path"
echo "  --with-curl=PATH         Specify libcurl installation path"
echo "  --with-sqlite=PATH       Specify SQLite3 installation path"
echo "  --enable-debug           Enable debug mode"
echo ""
echo "Example with Homebrew (macOS):"
echo "  ./configure --with-ruby=/opt/homebrew/opt/ruby \\"
echo "              --with-openssl=/opt/homebrew/opt/openssl"
echo ""
