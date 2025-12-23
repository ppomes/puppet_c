# Multi-stage Dockerfile for puppet_c
# Usage:
#   docker build --target server -t puppetc-server .
#   docker build --target agent -t puppetc-agent .

# =============================================================================
# Build stage
# =============================================================================
FROM debian:bookworm AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    autoconf \
    automake \
    libtool \
    flex \
    bison \
    pkg-config \
    libyaml-dev \
    libssl-dev \
    libmicrohttpd-dev \
    libcurl4-openssl-dev \
    ruby-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN autoreconf -i && \
    ./configure --prefix=/usr && \
    make -j$(nproc)

# =============================================================================
# Server image
# =============================================================================
FROM debian:bookworm-slim AS server

RUN apt-get update && apt-get install -y --no-install-recommends \
    libyaml-0-2 \
    libssl3 \
    libmicrohttpd12 \
    libruby3.1 \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Copy binaries
COPY --from=builder /build/server/.libs/puppetc-server /usr/bin/
COPY --from=builder /build/src/.libs/libpuppetc.so.0.0.0 /usr/lib/
COPY --from=builder /build/facter/.libs/libfacter_c.so.0.0.0 /usr/lib/

# Create symlinks
RUN ln -s libpuppetc.so.0.0.0 /usr/lib/libpuppetc.so.0 && \
    ln -s libpuppetc.so.0 /usr/lib/libpuppetc.so && \
    ln -s libfacter_c.so.0.0.0 /usr/lib/libfacter_c.so.0 && \
    ln -s libfacter_c.so.0 /usr/lib/libfacter_c.so && \
    ldconfig

# Create puppet directories
RUN mkdir -p /etc/puppet/manifests /etc/puppet/modules /etc/puppet/hiera

EXPOSE 8140

ENTRYPOINT ["puppetc-server"]
CMD ["-v", "/etc/puppet"]

# =============================================================================
# Agent image
# =============================================================================
FROM debian:bookworm-slim AS agent

RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl4 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy binaries
COPY --from=builder /build/agent/.libs/puppetc-agent /usr/bin/
COPY --from=builder /build/facter/.libs/libfacter_c.so.0.0.0 /usr/lib/

# Create symlinks
RUN ln -s libfacter_c.so.0.0.0 /usr/lib/libfacter_c.so.0 && \
    ln -s libfacter_c.so.0 /usr/lib/libfacter_c.so && \
    ldconfig

ENTRYPOINT ["puppetc-agent"]
CMD ["-s", "http://server:8140", "-n"]
