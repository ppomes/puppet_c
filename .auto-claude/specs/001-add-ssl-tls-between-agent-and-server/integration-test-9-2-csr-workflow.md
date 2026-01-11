# Integration Test: CSR Workflow (subtask-9-2)

**Test ID:** subtask-9-2
**Phase:** Integration Testing (Phase 9)
**Type:** End-to-End Integration Test
**Status:** Ready for Execution

## Test Overview

This integration test verifies the complete Certificate Signing Request (CSR) workflow between the Puppet agent and server:

1. Agent starts without a certificate
2. Agent generates a private key and CSR
3. Agent submits CSR to server
4. Server auto-signs the CSR
5. Agent retrieves the signed certificate
6. Verify the certificate is properly signed by the CA

## Prerequisites

### Code Verification ✓

**Agent CSR Implementation** (`agent/puppetc_agent.c`):
- ✓ `generate_private_key()` - Line 73: Generates 2048-bit RSA key using OpenSSL 3.0+ EVP API
- ✓ `create_certificate_request()` - Line 141: Creates X509_REQ with certname as CN field
- ✓ `csr_to_pem()` - Line 208: Converts CSR to PEM format using BIO memory buffer
- ✓ `submit_certificate_request()` - Line 245: POSTs CSR to `/puppet-ca/v1/certificate_request/:certname`
- ✓ `ensure_certificate()` - Line 349: Orchestrates full CSR workflow

**Server CSR Handler** (`server/puppetc_server.c`):
- ✓ `handle_certificate_request()` - EVHTP handler at line 873
- ✓ `handle_certificate_request()` - libmicrohttpd handler at line 1461
- ✓ Endpoint registered: `/puppet-ca/v1/certificate_request/:certname` (line 1928 EVHTP, line 1569 MHD)

**CA Signing Implementation** (`server/puppet_ca.c`):
- ✓ `puppet_ca_sign_csr()` - Line 523: Signs CSR and returns X.509 certificate
- ✓ CSR signature verification (line 564)
- ✓ Serial number management (line 587)
- ✓ Validity period: configurable via `PUPPET_CA_CERT_VALIDITY_DAYS`
- ✓ X.509 v3 extensions: Subject Key Identifier, Authority Key Identifier, Basic Constraints

**Configuration**:
- ✓ Agent SSL paths: `/var/lib/puppetc/ssl/certs/<certname>.pem`, `/var/lib/puppetc/ssl/private_keys/<certname>.pem`
- ✓ Server CA directory: `/etc/puppetc/ssl/ca/`
- ✓ Auto-signing: Implemented in `server/puppet_autosign.c` (modes: NONE, NAIVE, WHITELIST, POLICY)

## Test Environment Setup

### Step 1: Clean Environment

Remove any existing certificates to simulate a fresh agent:

```bash
# Stop all running containers
docker-compose down -v

# Remove any existing SSL volumes
docker volume rm $(docker volume ls -q | grep puppetc.*ssl) 2>/dev/null || true

# Verify clean state
docker volume ls | grep puppetc
# Should show no SSL volumes
```

### Step 2: Configure Auto-Signing

Create autosign configuration for testing (NAIVE mode for automatic signing):

```bash
# Create autosign config file in server volume
mkdir -p ./test-data/server-config
cat > ./test-data/server-config/autosign.conf <<EOF
# Auto-sign mode for integration testing
# Options: none, naive, whitelist, policy
mode = naive

# SECURITY WARNING: naive mode auto-signs ALL CSRs
# Use only for testing! Production should use 'policy' or 'whitelist'
EOF
```

**Alternative: Whitelist Mode** (more secure for testing):

```bash
cat > ./test-data/server-config/autosign.conf <<EOF
mode = whitelist
whitelist_file = /etc/puppetc/autosign-whitelist.txt
EOF

# Create whitelist file
cat > ./test-data/server-config/autosign-whitelist.txt <<EOF
# Whitelist of certnames allowed to auto-sign
test-agent.example.com
*.test.local
EOF
```

### Step 3: Start Server

Start the server and verify CA initialization:

```bash
# Start server in background
docker-compose up -d server

# Wait for server startup (check logs)
docker-compose logs -f server

# Expected log output:
# [INFO] Initializing CA at /etc/puppetc/ssl/ca
# [INFO] CA certificate does not exist, generating new CA
# [INFO] CA generated successfully
# [INFO] CA certificate saved to /etc/puppetc/ssl/ca/ca_crt.pem
# [INFO] Server listening on port 8140 (HTTPS)
```

Verify CA exists:

```bash
docker-compose exec server ls -la /etc/puppetc/ssl/ca/
# Expected:
# -rw-r--r-- 1 root root  1234 Jan 11 10:00 ca_crt.pem
# -rw------- 1 root root  1675 Jan 11 10:00 ca_key.pem
# -rw-r--r-- 1 root root     2 Jan 11 10:00 serial
```

## Test Execution

### Verification Step 1: Start Agent Without Certificate

Start the agent in verbose mode to see CSR workflow:

```bash
# Run agent with verbose logging
docker-compose run --rm agent puppetc-agent -v -n

# Expected output (verbose mode):
# [INFO] Certificate not found: /var/lib/puppetc/ssl/certs/<certname>.pem
# [INFO] Certificate not found, starting CSR workflow
# [INFO] Generating new private key
# [INFO] Private key generated successfully: /var/lib/puppetc/ssl/private_keys/<certname>.pem
# [INFO] Creating certificate signing request for: <certname>
# [INFO] CSR created successfully
# [INFO] Submitting CSR to https://server:8140/puppet-ca/v1/certificate_request/<certname>
```

**Success Criteria**:
- ✓ Agent detects missing certificate
- ✓ Agent starts CSR workflow automatically
- ✓ Agent logs clearly indicate CSR generation

### Verification Step 2: Agent Generates CSR and Submits to Server

Monitor the agent logs during CSR submission:

```bash
# Check agent container logs (if running in background)
docker-compose logs agent

# Expected log entries:
# [INFO] CSR created successfully
# [INFO] Submitting CSR to https://server:8140/puppet-ca/v1/certificate_request/<certname>
```

Verify private key was created with correct permissions:

```bash
# Check private key file
docker-compose run --rm agent ls -la /var/lib/puppetc/ssl/private_keys/

# Expected:
# -rw------- 1 root root  1675 Jan 11 10:05 <certname>.pem

# Verify it's a valid RSA key
docker-compose run --rm agent openssl rsa -in /var/lib/puppetc/ssl/private_keys/<certname>.pem -check -noout
# Expected: RSA key ok
```

**Success Criteria**:
- ✓ Private key file created with 0600 permissions (rw-------)
- ✓ Private key is valid RSA 2048-bit key
- ✓ CSR submitted to correct endpoint

### Verification Step 3: Server Auto-Signs CSR

Monitor server logs for CSR processing:

```bash
# Check server logs
docker-compose logs server | grep -i certificate

# Expected log entries:
# [INFO] Certificate request for: <certname>
# [INFO] Signing CSR for: <certname>
# [INFO] Successfully signed certificate for: <certname>
```

Verify signed certificate was stored on server:

```bash
# Check server's signed certificates directory
docker-compose exec server ls -la /etc/puppetc/ssl/ca/signed/

# Expected:
# -rw-r--r-- 1 root root  1234 Jan 11 10:05 <certname>.pem
```

**Success Criteria**:
- ✓ Server receives CSR and logs certificate request
- ✓ Server successfully signs CSR (auto-sign mode)
- ✓ Signed certificate stored in `/etc/puppetc/ssl/ca/signed/<certname>.pem`

### Verification Step 4: Agent Retrieves Signed Certificate

Check agent logs for certificate retrieval:

```bash
# Agent logs should show:
# [INFO] Server response: HTTP 200
# [INFO] Certificate saved to /var/lib/puppetc/ssl/certs/<certname>.pem
```

Verify certificate file exists on agent:

```bash
# Check certificate file
docker-compose run --rm agent ls -la /var/lib/puppetc/ssl/certs/

# Expected:
# -rw-r--r-- 1 root root  1234 Jan 11 10:05 <certname>.pem
```

**Success Criteria**:
- ✓ Agent receives HTTP 200 response from server
- ✓ Certificate saved to `/var/lib/puppetc/ssl/certs/<certname>.pem`
- ✓ Certificate file has 0644 permissions (rw-r--r--)

### Verification Step 5: Verify Certificate Signed by CA

Validate the certificate using OpenSSL:

```bash
# 1. Extract certificate details
docker-compose run --rm agent openssl x509 -in /var/lib/puppetc/ssl/certs/<certname>.pem -text -noout

# Expected output should include:
# Certificate:
#     Data:
#         Version: 3 (0x2)
#         Serial Number: 2 (0x2)
#         Signature Algorithm: sha256WithRSAEncryption
#         Issuer: CN = Puppet CA: puppetc-server
#         Subject: CN = <certname>
#         X509v3 extensions:
#             X509v3 Subject Key Identifier:
#             X509v3 Authority Key Identifier:
#                 keyid:...
#             X509v3 Basic Constraints:
#                 CA:FALSE
```

Verify certificate chain:

```bash
# 2. Verify certificate is signed by CA
docker-compose run --rm agent sh -c \
  "openssl verify -CAfile /var/lib/puppetc/ssl/ca/ca_crt.pem /var/lib/puppetc/ssl/certs/<certname>.pem"

# Expected output:
# /var/lib/puppetc/ssl/certs/<certname>.pem: OK
```

Check certificate subject matches certname:

```bash
# 3. Extract subject CN
docker-compose run --rm agent openssl x509 -in /var/lib/puppetc/ssl/certs/<certname>.pem -noout -subject

# Expected output:
# subject=CN = <certname>
```

Verify issuer is CA:

```bash
# 4. Extract issuer
docker-compose run --rm agent openssl x509 -in /var/lib/puppetc/ssl/certs/<certname>.pem -noout -issuer

# Expected output:
# issuer=CN = Puppet CA: puppetc-server
```

**Success Criteria**:
- ✓ Certificate is X.509 v3 format
- ✓ Certificate subject CN matches certname
- ✓ Certificate issuer matches CA subject
- ✓ Certificate verifies successfully against CA certificate
- ✓ Certificate has proper X.509 v3 extensions (Subject Key ID, Authority Key ID, Basic Constraints: CA:FALSE)
- ✓ Signature algorithm is SHA256 or stronger

## Automated Verification Script

For automated testing, use this script:

```bash
#!/bin/bash
# integration-test-csr-workflow.sh

set -e

CERTNAME="test-agent.example.com"
AGENT_CERT_PATH="/var/lib/puppetc/ssl/certs/${CERTNAME}.pem"
AGENT_KEY_PATH="/var/lib/puppetc/ssl/private_keys/${CERTNAME}.pem"
AGENT_CA_PATH="/var/lib/puppetc/ssl/ca/ca_crt.pem"
SERVER_SIGNED_CERT="/etc/puppetc/ssl/ca/signed/${CERTNAME}.pem"

echo "=== CSR Workflow Integration Test ==="
echo

# Step 1: Clean environment
echo "[1/6] Cleaning environment..."
docker-compose down -v 2>/dev/null || true
docker volume rm $(docker volume ls -q | grep puppetc.*ssl) 2>/dev/null || true
echo "✓ Clean environment ready"
echo

# Step 2: Configure auto-signing
echo "[2/6] Configuring auto-signing (NAIVE mode)..."
mkdir -p ./test-data/server-config
cat > ./test-data/server-config/autosign.conf <<EOF
mode = naive
EOF
echo "✓ Auto-sign configured"
echo

# Step 3: Start server
echo "[3/6] Starting server..."
docker-compose up -d server
sleep 5  # Wait for CA generation
docker-compose logs server | grep -i "CA generated successfully" || {
    echo "✗ CA generation failed"
    exit 1
}
echo "✓ Server started with CA"
echo

# Step 4: Run agent (CSR workflow)
echo "[4/6] Running agent (CSR workflow)..."
docker-compose run --rm -e PUPPET_CERTNAME="${CERTNAME}" agent puppetc-agent -v -n 2>&1 | tee /tmp/agent-output.log

# Check for CSR workflow in logs
grep -q "Certificate not found, starting CSR workflow" /tmp/agent-output.log || {
    echo "✗ CSR workflow not triggered"
    exit 1
}
grep -q "Certificate saved to" /tmp/agent-output.log || {
    echo "✗ Certificate not saved"
    exit 1
}
echo "✓ CSR workflow completed"
echo

# Step 5: Verify certificate files
echo "[5/6] Verifying certificate files..."

# Check private key permissions
PERMISSIONS=$(docker-compose run --rm agent stat -c "%a" "${AGENT_KEY_PATH}" 2>/dev/null | tr -d '\r')
if [ "$PERMISSIONS" != "600" ]; then
    echo "✗ Private key permissions incorrect: ${PERMISSIONS} (expected 600)"
    exit 1
fi
echo "✓ Private key has correct permissions (600)"

# Check certificate permissions
PERMISSIONS=$(docker-compose run --rm agent stat -c "%a" "${AGENT_CERT_PATH}" 2>/dev/null | tr -d '\r')
if [ "$PERMISSIONS" != "644" ]; then
    echo "✗ Certificate permissions incorrect: ${PERMISSIONS} (expected 644)"
    exit 1
fi
echo "✓ Certificate has correct permissions (644)"

# Verify RSA key
docker-compose run --rm agent openssl rsa -in "${AGENT_KEY_PATH}" -check -noout 2>&1 | grep -q "RSA key ok" || {
    echo "✗ Invalid RSA private key"
    exit 1
}
echo "✓ Valid RSA private key"
echo

# Step 6: Verify certificate chain
echo "[6/6] Verifying certificate chain..."

# Verify certificate against CA
docker-compose run --rm agent openssl verify -CAfile "${AGENT_CA_PATH}" "${AGENT_CERT_PATH}" 2>&1 | grep -q "OK" || {
    echo "✗ Certificate verification failed"
    exit 1
}
echo "✓ Certificate verified against CA"

# Check subject CN
SUBJECT_CN=$(docker-compose run --rm agent openssl x509 -in "${AGENT_CERT_PATH}" -noout -subject 2>/dev/null | grep -o "CN = .*" | cut -d'=' -f2 | tr -d ' \r')
if [ "$SUBJECT_CN" != "$CERTNAME" ]; then
    echo "✗ Subject CN mismatch: ${SUBJECT_CN} (expected ${CERTNAME})"
    exit 1
fi
echo "✓ Subject CN matches certname"

# Check issuer
docker-compose run --rm agent openssl x509 -in "${AGENT_CERT_PATH}" -noout -issuer 2>&1 | grep -q "CN = Puppet CA" || {
    echo "✗ Issuer does not match CA"
    exit 1
}
echo "✓ Issuer matches CA"

# Verify X.509 v3 extensions
docker-compose run --rm agent openssl x509 -in "${AGENT_CERT_PATH}" -noout -text 2>&1 | grep -q "X509v3 Subject Key Identifier" || {
    echo "✗ Missing X509v3 Subject Key Identifier extension"
    exit 1
}
echo "✓ X509v3 extensions present"

echo
echo "=== ALL TESTS PASSED ==="
echo "✓ Agent generated CSR successfully"
echo "✓ Server auto-signed CSR"
echo "✓ Agent retrieved signed certificate"
echo "✓ Certificate verified against CA"
echo "✓ All file permissions correct"
```

Make the script executable and run it:

```bash
chmod +x ./.auto-claude/specs/001-add-ssl-tls-between-agent-and-server/integration-test-csr-workflow.sh
./.auto-claude/specs/001-add-ssl-tls-between-agent-and-server/integration-test-csr-workflow.sh
```

## Success Criteria Checklist

- [ ] **Agent Detects Missing Certificate**: Agent logs show "Certificate not found, starting CSR workflow"
- [ ] **Private Key Generated**: RSA 2048-bit key created at `/var/lib/puppetc/ssl/private_keys/<certname>.pem` with 0600 permissions
- [ ] **CSR Created**: X509_REQ created with certname in CN field
- [ ] **CSR Submitted**: POST request to `/puppet-ca/v1/certificate_request/<certname>` successful
- [ ] **Server Receives CSR**: Server logs show "Certificate request for: <certname>"
- [ ] **Server Signs CSR**: Server logs show "Successfully signed certificate for: <certname>"
- [ ] **Certificate Stored on Server**: Signed cert saved to `/etc/puppetc/ssl/ca/signed/<certname>.pem`
- [ ] **Agent Receives Certificate**: HTTP 200 response with signed certificate in PEM format
- [ ] **Certificate Saved**: Certificate saved to `/var/lib/puppetc/ssl/certs/<certname>.pem` with 0644 permissions
- [ ] **Certificate Valid**: `openssl verify` confirms certificate is signed by CA
- [ ] **Subject Matches**: Certificate subject CN equals certname
- [ ] **Issuer Matches**: Certificate issuer equals CA subject
- [ ] **X.509 v3 Extensions**: Certificate includes Subject Key ID, Authority Key ID, Basic Constraints
- [ ] **Strong Signature**: Certificate uses SHA256 or stronger signature algorithm

## Troubleshooting

### Issue: Agent fails with "Failed to submit CSR"

**Possible Causes**:
- Server not running or not accessible
- Network connectivity issues
- Server CA not initialized

**Resolution**:
```bash
# Check server status
docker-compose ps server
docker-compose logs server

# Verify network connectivity
docker-compose exec agent ping -c 3 server

# Check server CA
docker-compose exec server ls /etc/puppetc/ssl/ca/
```

### Issue: Server returns HTTP 503 "CA not initialized"

**Possible Causes**:
- CA generation failed
- CA directory permissions incorrect

**Resolution**:
```bash
# Check CA initialization logs
docker-compose logs server | grep -i "CA"

# Manually verify CA files
docker-compose exec server ls -la /etc/puppetc/ssl/ca/

# Check directory permissions
docker-compose exec server stat -c "%a" /etc/puppetc/ssl/ca/
```

### Issue: Certificate verification fails

**Possible Causes**:
- Agent has different CA certificate than server
- Certificate corrupted during transfer

**Resolution**:
```bash
# Compare CA certificates
docker-compose exec server md5sum /etc/puppetc/ssl/ca/ca_crt.pem
docker-compose run --rm agent md5sum /var/lib/puppetc/ssl/ca/ca_crt.pem
# MD5 sums should match

# Re-download CA certificate
docker-compose exec server cat /etc/puppetc/ssl/ca/ca_crt.pem | \
  docker-compose run --rm -T agent sh -c "cat > /var/lib/puppetc/ssl/ca/ca_crt.pem"
```

### Issue: Private key permissions incorrect

**Resolution**:
```bash
# Fix permissions
docker-compose exec agent chmod 600 /var/lib/puppetc/ssl/private_keys/<certname>.pem
docker-compose exec agent chown root:root /var/lib/puppetc/ssl/private_keys/<certname>.pem
```

## Related Tests

- **subtask-9-1**: Server CA generation on first startup
- **subtask-9-3**: mTLS catalog compilation with HTTPS
- **subtask-9-4**: Certificate validation failures

## Notes

- This test uses **NAIVE auto-signing mode** for simplicity. Production environments should use **POLICY** or **WHITELIST** mode.
- The CSR workflow is triggered automatically when the agent detects a missing certificate.
- The private key remains on the agent and is never transmitted to the server.
- The server only receives the CSR (public key + metadata) and returns the signed certificate.
- Certificate validity period is controlled by `PUPPET_CA_CERT_VALIDITY_DAYS` (default: 3650 days / 10 years).

## References

- Agent CSR implementation: `agent/puppetc_agent.c` (lines 73-424)
- Server CSR handler: `server/puppetc_server.c` (lines 873-942 EVHTP, 1461-1500 MHD)
- CA signing logic: `server/puppet_ca.c` (lines 523-700)
- Spec section: Certificate Signing Request (CSR) Workflow
