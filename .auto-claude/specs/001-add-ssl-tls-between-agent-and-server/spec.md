# Specification: Add SSL/TLS Mutual Authentication Between Agent and Server

## Overview

This task implements mutual TLS (mTLS) authentication between the Puppet agent and server, matching real Puppet's security model. The implementation includes a shared Certificate Authority (CA), PKI infrastructure on the server side for signing client certificates, and configurable auto-signing capabilities. This ensures secure, authenticated communication between all agent-server interactions while maintaining behavioral parity with production Puppet deployments.

## Workflow Type

**Type**: feature

**Rationale**: This is a new security feature that adds substantial functionality to the existing agent-server communication architecture. It requires implementing certificate management infrastructure, modifying network communication layers, updating build/deployment artifacts, and introducing new configuration options.

## Task Scope

### Services Involved
- **puppetc-agent** (primary) - Client-side mTLS implementation, certificate request workflow
- **puppetc-server** (primary) - Server-side mTLS implementation, certificate signing service, PKI management
- **Docker build system** (integration) - Multi-stage Dockerfile updates for certificate management
- **Debian packaging** (integration) - Package dependencies and installation scripts
- **Vagrant/Multipass** (integration) - Development environment updates

### This Task Will:
- [x] Implement mutual TLS authentication using OpenSSL on both agent and server
- [x] Create shared CA infrastructure with certificate generation and signing capabilities
- [x] Add certificate signing request (CSR) workflow where agents request certificates from server
- [x] Implement configurable auto-signing feature (policy-based, whitelist, or naive modes)
- [x] Modify libcurl (agent) to use client certificates for HTTPS connections
- [x] Resolve libmicrohttpd/GnuTLS conflict and implement server-side HTTPS with OpenSSL
- [x] Update Docker images to include CA setup and certificate management
- [x] Update Debian packages for OpenSSL dependencies and certificate storage
- [x] Update Vagrant and Multipass configurations for secure development environments
- [x] Add certificate storage locations and lifecycle management
- [x] Implement error handling for certificate validation failures

### Out of Scope:
- Certificate revocation lists (CRLs) or OCSP support
- Certificate rotation/renewal automation
- Integration with external PKI systems
- Web UI for certificate management
- Certificate fingerprint pinning beyond standard X.509 validation

## Service Context

### puppetc-agent

**Tech Stack:**
- Language: C
- Framework: libcurl (OpenSSL variant)
- Key directories: `agent/`, `common/`

**Entry Point:** `agent/puppetc_agent.c`

**How to Run:**
```bash
puppetc-agent -n  # noop mode
puppetc-agent -a  # apply mode
```

**Port:** Client (connects to server on 8140)

**Current Communication Pattern:**
- Uses libcurl for HTTP POST to server
- Sends facts and receives compiled catalogs
- Currently HTTP-only (no encryption or authentication)

### puppetc-server

**Tech Stack:**
- Language: C
- Framework: libmicrohttpd (⚠️ GnuTLS-only for HTTPS)
- Key directories: `server/`, `common/`

**Entry Point:** `server/puppetc_server.c`

**How to Run:**
```bash
puppetc-server -v -m /etc/puppet/modules -D /etc/puppet/hiera /etc/puppet
```

**Port:** 8140

**Current API Endpoints:**
- `GET /status` - Health check
- `POST /puppet/v4/catalog` - Compile catalog

## Critical Architecture Decision Required

### libmicrohttpd TLS Limitation

**Problem**: libmicrohttpd (current HTTP server library) ONLY supports GnuTLS for TLS, which directly conflicts with the OpenSSL requirement.

**Options**:
1. **Replace libmicrohttpd** with OpenSSL-compatible HTTP server library
   - Options: libevhtp, libevent with evhttps, custom implementation
   - Pro: Single TLS dependency (OpenSSL only)
   - Con: Significant refactoring of server code

2. **Accept dual TLS dependency** (OpenSSL + GnuTLS)
   - OpenSSL for certificate operations
   - GnuTLS for libmicrohttpd HTTPS
   - Pro: Minimal code changes
   - Con: Two TLS libraries in dependency tree, larger binary size

3. **Fork/patch libmicrohttpd** for OpenSSL support
   - Pro: Keeps libmicrohttpd API
   - Con: Maintenance burden, complex undertaking

**Recommended Approach**: Option 1 - Replace libmicrohttpd with a lightweight OpenSSL-compatible HTTP server library. This maintains architectural cleanliness and satisfies the strict OpenSSL-only requirement.

## Files to Modify

| File | Service | What to Change |
|------|---------|---------------|
| `agent/puppetc_agent.c` | agent | Add libcurl mTLS options: CURLOPT_SSLCERT, CURLOPT_SSLKEY, CURLOPT_CAINFO, SSL verification flags |
| `server/puppetc_server.c` | server | Replace libmicrohttpd with OpenSSL-compatible HTTP server, add TLS context initialization, implement certificate validation |
| `server/puppet_ca.c` (new) | server | Certificate Authority implementation: CA cert/key generation, CSR signing, certificate storage |
| `server/puppet_autosign.c` (new) | server | Auto-signing logic: policy-based validation, whitelist checking, configuration parsing |
| `common/puppet_ssl.c` (new) | common | Shared SSL/TLS utilities: OpenSSL initialization, certificate loading, validation helpers |
| `include/puppet_ssl.h` (new) | common | Header for SSL utilities and structures |
| `include/puppet_ca.h` (new) | server | Header for CA operations |
| `configure.ac` | build | Add checks for new HTTP server library (if replacing libmicrohttpd) |
| `Dockerfile` | docker | Add CA initialization steps, certificate directory creation, OpenSSL tools |
| `docker-compose.yml` | docker | Add volume mounts for certificate storage, environment variables for CA paths |
| `vagrant/Vagrantfile` | vagrant | Add certificate setup provisioning steps |
| `multipass/cloud-init.yaml` | multipass | Add OpenSSL tools and certificate directory setup |
| `debian/control` | debian | Update dependencies if replacing libmicrohttpd |
| `debian/puppetc-server.postinst` (new) | debian | Post-installation script to initialize CA if not exists |
| `debian/puppetc-agent.postinst` (new) | debian | Post-installation script to create certificate directories |

## Files to Reference

These files show patterns to follow:

| File | Pattern to Copy |
|------|----------------|
| `compiler/puppet_stdlib.c` | OpenSSL EVP API usage for OpenSSL 3.0+ compatibility (lines 4290-4298, 4334-4342) |
| `agent/puppetc_agent.c` | libcurl option setting pattern (lines 123-139, 192-196) |
| `server/puppetc_server.c` | HTTP request handling pattern with MHD (lines 55-93) |
| `common/config_parser.c` | Configuration file parsing pattern for new autosign config |
| `Dockerfile` | Multi-stage build pattern (builder → server/agent) |

## Patterns to Follow

### OpenSSL EVP API (OpenSSL 3.0+ Compatible)

From `compiler/puppet_stdlib.c`:

```c
/* Use EVP API (OpenSSL 3.0+ compatible) */
unsigned char hash[EVP_MAX_MD_SIZE];
unsigned int hash_len;
EVP_MD_CTX *ctx = EVP_MD_CTX_new();
if (ctx) {
    EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(ctx, data, data_len);
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}
```

**Key Points:**
- Use EVP API instead of deprecated SHA1(), MD5() functions
- Always check ctx != NULL before use
- Free context with EVP_MD_CTX_free() to prevent memory leaks
- This pattern is compatible with both OpenSSL 1.1+ and 3.0+

### libcurl TLS Configuration

From `agent/puppetc_agent.c` (to be added):

```c
curl_easy_setopt(curl, CURLOPT_SSLCERT, client_cert_path);
curl_easy_setopt(curl, CURLOPT_SSLKEY, client_key_path);
curl_easy_setopt(curl, CURLOPT_CAINFO, ca_cert_path);
curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
```

**Key Points:**
- Set all three cert paths: client cert, client key, CA cert
- ALWAYS enable verification (VERIFYPEER=1, VERIFYHOST=2) for security
- Proper error handling if certificates don't exist or are invalid

### OpenSSL Initialization Pattern

```c
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>

/* Initialize OpenSSL (call once at startup) */
void puppet_ssl_init(void) {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
}

/* Cleanup OpenSSL (call at shutdown) */
void puppet_ssl_cleanup(void) {
    EVP_cleanup();
    ERR_free_strings();
}
```

**Key Points:**
- Call initialization functions before any OpenSSL operations
- Cleanup prevents memory leaks
- Thread-safe in OpenSSL 1.1+ (no manual locking needed)

## Requirements

### Functional Requirements

1. **Mutual TLS Authentication**
   - Description: Both agent and server must authenticate using X.509 certificates signed by shared CA
   - Acceptance: Agent connections with invalid/missing certificates are rejected; server verifies client cert against CA

2. **Certificate Authority (CA) Infrastructure**
   - Description: Server maintains CA private key and certificate; signs agent certificate requests
   - Acceptance: CA cert/key generated on first server startup; CA cert available for agent download

3. **Certificate Signing Request (CSR) Workflow**
   - Description: Agent generates private key + CSR, submits to server, retrieves signed certificate
   - Acceptance: New endpoint `POST /puppet-ca/v1/certificate_request/:certname` accepts CSR and returns signed cert

4. **Auto-Signing Configuration**
   - Description: Server can auto-sign CSRs based on policy (executable), whitelist (file), or naive (all) modes
   - Acceptance: Config file `/etc/puppetc/autosign.conf` controls behavior; policy executable receives CSR details via stdin

5. **Encrypted Communication**
   - Description: All agent-server traffic encrypted using TLS 1.2+
   - Acceptance: Wireshark capture shows TLS handshake and encrypted application data

6. **Certificate Storage**
   - Description: Agent stores cert/key in `/var/lib/puppetc/ssl/certs` and `/var/lib/puppetc/ssl/private_keys`; server stores CA in `/etc/puppetc/ssl/ca`
   - Acceptance: Proper filesystem permissions (0644 for certs, 0600 for keys, 0700 for directories)

### Edge Cases

1. **Missing CA on Server Startup** - Generate new CA if `/etc/puppetc/ssl/ca/ca_crt.pem` doesn't exist
2. **Missing Agent Certificate** - Agent initiates CSR workflow before catalog requests
3. **Expired Certificates** - Server rejects connections; agent logs clear error message and exits
4. **Hostname Mismatch** - Certificate CN/SAN must match agent's certname or connection fails
5. **Invalid Auto-Sign Policy** - If policy executable returns non-zero, CSR is queued for manual signing
6. **CA Certificate Rotation** - Out of scope for v1 (manual process)
7. **Concurrent CSR Requests** - Server must handle multiple simultaneous CSR submissions safely
8. **Network Failures During CSR Workflow** - Agent should retry CSR submission if network is unreachable; clear error message if server unavailable for extended period

## Implementation Notes

### DO
- Follow OpenSSL EVP API patterns from `puppet_stdlib.c` for maximum compatibility
- Use `curl_easy_setopt()` pattern from `puppetc_agent.c` for consistent libcurl configuration
- Implement policy-based autosigning (most secure) as the recommended default
- Store CA private key with restrictive permissions (0600, root-only)
- Initialize OpenSSL library at program startup (both agent and server)
- Use HTTPS URLs (https://server:8140) instead of HTTP after TLS implementation
- Add proper error messages for certificate validation failures
- Document certificate paths and workflow in configuration files
- Test with self-signed CA certificates (common in Puppet deployments)

### DON'T
- Use deprecated OpenSSL functions (SHA1(), MD5() direct calls) - use EVP API instead
- Skip certificate verification (CURLOPT_SSL_VERIFYPEER=0) - massive security hole
- Hardcode certificate paths - make them configurable
- Mix GnuTLS and OpenSSL in the same TLS context
- Store private keys in world-readable locations
- Implement naive auto-signing as default (security risk)
- Forget to call `SSL_library_init()` before OpenSSL operations
- Use basic autosign (whitelist-only) in production recommendations

## Development Environment

### Start Services

```bash
# Build all Docker images
docker-compose build

# Start server (with CA initialization)
docker-compose up -d server

# Run agent (one-shot, noop mode)
docker-compose run agent

# Or use interactive shell
docker-compose up -d agent-shell
docker-compose exec agent-shell bash
```

### Service URLs
- Server HTTPS: https://localhost:8140 (after TLS implementation)
- Server HTTP (legacy): http://localhost:8140 (to be removed)
- Health check: https://localhost:8140/status
- CSR endpoint: https://localhost:8140/puppet-ca/v1/certificate_request/:certname

### Required Environment Variables
- `PUPPET_SERVER`: Server URL (default: https://localhost:8140)
- `PUPPET_CA_PATH`: CA certificate path (default: /etc/puppetc/ssl/ca/ca_crt.pem)
- `PUPPET_SSL_DIR`: SSL directory for agent certs (default: /var/lib/puppetc/ssl)
- `PUPPET_AUTOSIGN`: Auto-sign policy path (default: /etc/puppetc/autosign.conf)

### Certificate Directory Structure

**Server:**
```
/etc/puppetc/
├── autosign.conf       # Auto-signing configuration
└── ssl/
    └── ca/
        ├── ca_crt.pem          # CA certificate (public)
        ├── ca_key.pem          # CA private key (0600)
        ├── serial              # Serial number for next cert
        └── signed/             # Signed client certificates
            └── <certname>.pem
```

**Agent:**
```
/var/lib/puppetc/ssl/
├── ca/
│   └── ca_crt.pem      # CA certificate (copied from server)
├── certs/
│   └── <certname>.pem  # Client certificate
└── private_keys/
    └── <certname>.pem  # Client private key (0600)
```

## Success Criteria

The task is complete when:

1. [x] Agent successfully establishes mTLS connection to server using signed certificate
2. [x] Server generates CA certificate/key on first startup if not present
3. [x] Agent can request certificate signing via new CSR endpoint
4. [x] Server auto-signs CSRs based on configured policy (policy-based, whitelist, or naive)
5. [x] All agent-server traffic encrypted with TLS 1.2+ (verified via Wireshark/tcpdump)
6. [x] Certificate validation failures produce clear, actionable error messages
7. [x] Docker images build successfully with CA initialization
8. [x] Vagrant and Multipass VMs can establish secure agent-server communication
9. [x] Debian packages install with proper certificate directory setup
10. [x] No console errors during normal operation
11. [x] Existing catalog compilation tests still pass
12. [x] New functionality verified via browser/curl (CSR endpoint, HTTPS catalog requests)

## QA Acceptance Criteria

**CRITICAL**: These criteria must be verified by the QA Agent before sign-off.

### Unit Tests

| Test | File | What to Verify |
|------|------|----------------|
| CA certificate generation | `tests/test_puppet_ca.c` | CA cert/key created with correct format, RSA 2048+ bits, proper X.509 fields |
| CSR creation | `tests/test_puppet_ca.c` | Agent can create valid CSR with certname in CN field |
| Certificate signing | `tests/test_puppet_ca.c` | Server signs CSR and returns valid X.509 certificate |
| Auto-sign policy evaluation | `tests/test_puppet_autosign.c` | Policy-based, whitelist, and naive modes work correctly |
| SSL context initialization | `tests/test_puppet_ssl.c` | OpenSSL library initializes without errors, memory cleanup works |
| Certificate validation | `tests/test_puppet_ssl.c` | Invalid/expired/mismatched certs properly rejected |

### Integration Tests

| Test | Services | What to Verify |
|------|----------|----------------|
| Agent → Server mTLS handshake | agent ↔ server | Full TLS handshake completes, both parties authenticated |
| CSR submission and signing | agent ↔ server | Agent submits CSR, receives signed cert, can use it for catalog request |
| Certificate rejection | agent ↔ server | Invalid cert fails handshake with clear error message |
| Auto-sign policy execution | agent ↔ server | Policy executable invoked correctly, CSR signed/rejected based on return code |

### End-to-End Tests

| Flow | Steps | Expected Outcome |
|------|-------|------------------|
| First-time agent setup | 1. Start server (CA generated) 2. Start agent (no cert) 3. Agent requests CSR signing 4. Agent retrieves catalog | Agent successfully compiles and applies catalog |
| Server certificate validation | 1. Agent with valid cert 2. Request catalog | HTTPS connection succeeds, catalog returned |
| Agent with invalid cert | 1. Agent with self-signed cert (not CA-signed) 2. Request catalog | Connection rejected, error logged |
| Auto-sign whitelist | 1. Configure whitelist with specific certname 2. Agent CSR request | CSR auto-signed for whitelisted, rejected for others |

### Browser/CLI Verification

| Component | URL/Command | Checks |
|-----------|-------------|--------|
| Server status | `curl -k https://localhost:8140/status` | Returns JSON status over HTTPS |
| CSR endpoint | `curl -k --data @csr.pem https://localhost:8140/puppet-ca/v1/certificate_request/test-node` | Accepts CSR, returns signed certificate |
| mTLS catalog request | `curl --cert client.pem --key client-key.pem --cacert ca.pem https://localhost:8140/puppet/v4/catalog --data @request.json` | Authenticated request succeeds |
| Certificate validation | `openssl x509 -in /etc/puppetc/ssl/ca/ca_crt.pem -text -noout` | Valid CA certificate with correct fields |
| TLS handshake | `openssl s_client -connect localhost:8140 -CAfile ca.pem -cert client.pem -key client-key.pem` | mTLS handshake successful, shows certificate chain |

### Docker Verification

| Check | Command | Expected |
|-------|---------|----------|
| CA generation in server image | `docker-compose up -d server && docker-compose exec server ls /etc/puppetc/ssl/ca/` | ca_crt.pem and ca_key.pem exist |
| Agent SSL directories | `docker-compose run agent ls -la /var/lib/puppetc/ssl` | Directories exist with proper permissions |
| Server HTTPS port | `docker-compose ps` | Port 8140 mapped and accessible |
| mTLS connection | `docker-compose run agent puppetc-agent -a` | Agent successfully connects and applies catalog |

### Vagrant/Multipass Verification

| Check | Command | Expected |
|-------|---------|----------|
| Vagrant server setup | `cd vagrant && vagrant up server && vagrant ssh server -c "sudo ls /etc/puppetc/ssl/ca/"` | CA files exist |
| Vagrant agent connection | `vagrant ssh agent -c "sudo puppetc-agent -n"` | Noop run succeeds with TLS |
| Multipass VM packages | `multipass exec puppetc -- dpkg -l \| grep puppetc` | All packages installed |
| Multipass SSL | `multipass exec puppetc -- ls /var/lib/puppetc/ssl` | SSL directories present |

### Debian Package Verification

| Check | Command | Expected |
|-------|---------|----------|
| Package dependencies | `dpkg-deb -I puppetc-server_*.deb \| grep Depends` | libssl3 (or libssl1.1) listed |
| Post-install script | `dpkg-deb -e puppetc-server_*.deb /tmp/extract && cat /tmp/extract/postinst` | CA initialization logic present |
| File permissions | `dpkg-deb -c puppetc-server_*.deb \| grep ca_key` | No private key shipped in package |

### Security Verification

| Check | Method | Expected |
|-------|--------|----------|
| TLS version | `openssl s_client -connect localhost:8140` | TLSv1.2 or TLSv1.3 negotiated |
| Cipher suite | Same as above | Strong cipher (ECDHE-RSA-AES256-GCM-SHA384 or similar) |
| Certificate expiry | `openssl x509 -in ca_crt.pem -noout -dates` | Valid for reasonable period (e.g., 10 years for CA) |
| Private key permissions | `ls -la /etc/puppetc/ssl/ca/ca_key.pem` | 0600 (rw-------) |
| SSL directory permissions | `ls -ld /var/lib/puppetc/ssl/private_keys` | 0700 (rwx------) |

### QA Sign-off Requirements

- [ ] All unit tests pass
- [ ] All integration tests pass
- [ ] All E2E tests pass
- [ ] Browser/CLI verification complete (HTTPS endpoints respond correctly)
- [ ] Docker image verification complete (CA initialized, mTLS works)
- [ ] Vagrant/Multipass verification complete (both environments work)
- [ ] Debian package verification complete (dependencies correct, post-install works)
- [ ] Security verification complete (TLS version, ciphers, permissions)
- [ ] No regressions in existing catalog compilation functionality
- [ ] Code follows established OpenSSL EVP API patterns from `puppet_stdlib.c`
- [ ] No security vulnerabilities introduced (no cert verification bypasses, no world-readable keys)
- [ ] Documentation updated (README, example configs for autosign policies)

## References

### Puppet SSL/TLS Documentation
- Puppet SSL Configuration: https://puppet.com/docs/puppet/latest/config_file_auth.html
- Puppet Certificate Authority: https://puppet.com/docs/puppet/latest/ssl_certificates.html
- Puppet Autosigning: https://puppet.com/docs/puppet/latest/ssl_autosign.html

### OpenSSL Documentation
- EVP API (OpenSSL 3.0): https://www.openssl.org/docs/man3.0/man7/evp.html
- X.509 Certificate Functions: https://www.openssl.org/docs/man3.0/man3/X509_sign.html
- SSL/TLS Programming: https://www.openssl.org/docs/man3.0/man7/ssl.html

### libcurl TLS Options
- CURLOPT_SSLCERT: https://curl.se/libcurl/c/CURLOPT_SSLCERT.html
- CURLOPT_SSL_VERIFYPEER: https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html
