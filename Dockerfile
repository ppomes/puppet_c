# Multi-stage Dockerfile for puppet_c
# Usage:
#   docker build --target server -t puppetc-server .
#   docker build --target agent -t puppetc-agent .

# =============================================================================
# Build stage - creates .deb packages
# =============================================================================
FROM debian:bookworm AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    autoconf \
    automake \
    libtool \
    flex \
    bison \
    gawk \
    pkg-config \
    libyaml-dev \
    libssl-dev \
    libmicrohttpd-dev \
    libcurl4-openssl-dev \
    ruby-dev \
    debhelper \
    devscripts \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

# Build Debian packages
RUN autoreconf -i && \
    dpkg-buildpackage -us -uc -b

# Move packages to a known location
RUN mkdir -p /packages && mv /puppet-c*.deb /lib*.deb /packages/ 2>/dev/null || mv ../*.deb /packages/

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

# Copy and install packages
COPY --from=builder /packages/libpuppetc-common0_*.deb /tmp/
COPY --from=builder /packages/libpuppetc0_*.deb /tmp/
COPY --from=builder /packages/puppetc-server_*.deb /tmp/

RUN dpkg -i /tmp/libpuppetc-common0_*.deb \
            /tmp/libpuppetc0_*.deb \
            /tmp/puppetc-server_*.deb && \
    rm -rf /tmp/*.deb

# Create puppet directories
RUN mkdir -p /etc/puppet/manifests /etc/puppet/modules /etc/puppet/hiera

EXPOSE 8140

ENTRYPOINT ["puppetc-server"]
CMD ["-v", "-m", "/etc/puppet/modules", "-D", "/etc/puppet/hiera", "/etc/puppet"]

# =============================================================================
# Agent image
# =============================================================================
FROM debian:bookworm-slim AS agent

RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl4 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy and install packages
COPY --from=builder /packages/libpuppetc-common0_*.deb /tmp/
COPY --from=builder /packages/libfacter-c0_*.deb /tmp/
COPY --from=builder /packages/puppetc-agent_*.deb /tmp/

RUN dpkg -i /tmp/libpuppetc-common0_*.deb \
            /tmp/libfacter-c0_*.deb \
            /tmp/puppetc-agent_*.deb && \
    rm -rf /tmp/*.deb

ENTRYPOINT ["puppetc-agent"]
# Default: noop mode. Use -a to apply.
# Set PUPPET_SERVER env var or use -s to specify server.
CMD ["-n"]
