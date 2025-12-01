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
echo "  --enable-ruby      Enable Ruby/ERB support (default: auto-detect)"
echo "  --with-ruby=PATH   Specify Ruby installation path"
echo "  --enable-debug     Enable debug mode"
echo "  --disable-ruby     Disable Ruby support"
echo ""
echo "Example with Homebrew Ruby:"
echo "  ./configure --with-ruby=/opt/homebrew/opt/ruby"
echo ""