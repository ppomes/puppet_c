# Docker Build Verification for subtask-6-3

## Overview
This document provides verification steps for Docker build and CA initialization that should be performed in an environment where Docker commands are available.

## Pre-Build Verification (Completed)

### 1. Source Files Present ✓
- [x] `./common/puppet_ssl.c` (15K) - SSL utilities implementation
- [x] `./server/puppet_ca.c` (32K) - CA implementation
- [x] `./server/puppet_autosign.c` (13K) - Auto-signing logic
- [x] `./include/puppet_ssl.h` (10K) - SSL headers
- [x] `./include/puppet_ca.h` (12K) - CA headers

### 2. Makefile Configuration ✓
- [x] `server/Makefile.am` includes `puppet_ca.c` and `puppet_autosign.c`
- [x] `common/Makefile.am` includes `puppet_ssl.c`
- [x] SSL libraries linked: `-lssl -lcrypto` in common/Makefile.am
- [x] OpenSSL flags: `$(OPENSSL_CFLAGS)` and `$(OPENSSL_LIBS)` in server/Makefile.am

### 3. Dockerfile Configuration ✓
- [x] Builder stage has OpenSSL build dependencies (`libssl-dev`)
- [x] Server image has OpenSSL runtime (`libssl3` line 48, `openssl` line 53)
- [x] Agent image has OpenSSL tools (`openssl` line 91)
- [x] SSL directories created for server: `/etc/puppetc/ssl/ca`, `/etc/puppetc/ssl/certs`, `/etc/puppetc/ssl/private` (lines 71-72)
- [x] SSL directories created for agent: `/etc/puppetc/ssl/certs`, `/etc/puppetc/ssl/private` (lines 104-105)
- [x] Build uses `autoreconf -i && dpkg-buildpackage` (lines 34-35)

### 4. docker-compose.yml Configuration ✓
- [x] Server has SSL volume mount: `puppetc-server-ssl:/etc/puppetc/ssl`
- [x] Agent has SSL volume mount: `puppetc-agent-ssl:/var/lib/puppetc/ssl`
- [x] Environment variables set:
  - `PUPPET_SERVER=https://server:8140` (HTTPS enabled)
  - `PUPPET_CA_PATH=/etc/puppetc/ssl/ca/ca_crt.pem`
  - `PUPPET_SSL_DIR=/var/lib/puppetc/ssl`
- [x] Named volumes for SSL persistence: `puppetc-server-ssl`, `puppetc-agent-ssl`, `puppetc-agent-shell-ssl`

### 5. Server CA Initialization Code ✓
Verified in `server/puppetc_server.c` (lines 1824-1859):
- [x] CA context initialized with `puppet_ca_init(ca_dir)`
- [x] Checks if CA exists with `puppet_ca_exists(ca_ctx)`
- [x] Generates new CA if not found: `puppet_ca_generate(ca_ctx, ca_subject, PUPPET_CA_VALIDITY_DAYS)`
- [x] Saves CA: `puppet_ca_save(ca_ctx)`
- [x] Loads existing CA: `puppet_ca_load(ca_ctx)`
- [x] Verbose logging for CA operations
- [x] Proper error handling with `puppet_ca_get_error(ca_ctx)`

## Build Verification Steps (To Be Run Manually)

Since Docker commands are not available in this automated environment, these steps should be performed manually:

### Step 1: Build Server Image
```bash
docker-compose build server 2>&1 | tee build-server.log
```

**Expected outcomes:**
- Build completes successfully (exit code 0)
- No fatal errors in build log
- Builder stage completes: autoreconf, configure, make, dpkg-buildpackage
- Server image includes OpenSSL tools
- SSL directories created in image

**Verification:**
```bash
# Check last lines for success
tail -5 build-server.log

# Verify no errors
grep -i "error\|fail" build-server.log | grep -v "warning"
```

### Step 2: Start Server and Verify CA Initialization
```bash
docker-compose up -d server

# Wait for startup
sleep 5

# Check logs for CA initialization
docker-compose logs server
```

**Expected log output:**
```
[INFO] CA not found, generating new CA...
[INFO] CA generated successfully
```
OR (if CA already exists):
```
[INFO] CA loaded successfully
```

### Step 3: Verify CA Files Created
```bash
docker-compose exec server ls -la /etc/puppetc/ssl/ca/
```

**Expected output:**
- `ca_crt.pem` (CA certificate, permissions 0644)
- `ca_key.pem` (CA private key, permissions 0600)
- `serial` (serial number file, permissions 0644)

### Step 4: Verify CA Certificate
```bash
docker-compose exec server openssl x509 -in /etc/puppetc/ssl/ca/ca_crt.pem -text -noout
```

**Expected output:**
- Subject: `CN=Puppet CA: puppetc-server`
- Validity: 10 years (3650 days)
- RSA key size: 2048 bits or greater
- Signature Algorithm: sha256WithRSAEncryption
- X509v3 extensions include Basic Constraints: CA:TRUE

### Step 5: Verify Server HTTPS Endpoint
```bash
# Check if server is listening on port 8140
docker-compose exec server netstat -tlnp | grep 8140

# Test status endpoint (may need to accept self-signed cert)
curl -k https://localhost:8140/status
```

**Expected:**
- Port 8140 is listening
- Status endpoint returns JSON response

### Step 6: Build Agent Image
```bash
docker-compose build agent 2>&1 | tee build-agent.log
tail -5 build-agent.log
```

**Expected:**
- Build completes successfully
- Agent image includes OpenSSL tools
- SSL directories created

### Step 7: Test Agent SSL Directory Setup
```bash
docker-compose run --rm agent ls -la /var/lib/puppetc/ssl/
```

**Expected:**
- Directories exist (may be empty initially)
- Proper permissions for certificate storage

## Success Criteria

✓ **Build Phase:**
- [ ] `docker-compose build server` completes without errors
- [ ] `docker-compose build agent` completes without errors
- [ ] Both images include OpenSSL tools

✓ **CA Initialization:**
- [ ] Server generates CA on first startup
- [ ] CA certificate and private key created in `/etc/puppetc/ssl/ca/`
- [ ] CA certificate is valid X.509 certificate
- [ ] CA private key has secure permissions (0600)

✓ **Configuration:**
- [ ] SSL volumes persist across container restarts
- [ ] Environment variables correctly set for HTTPS
- [ ] Server listens on port 8140

## Troubleshooting

### If build fails:
1. Check that all source files exist and are valid C code
2. Verify Makefile.am files have correct source listings
3. Check configure.ac has OpenSSL dependency checks
4. Review build log for specific compilation errors

### If CA initialization fails:
1. Check server logs: `docker-compose logs server`
2. Verify CA directory exists and is writable
3. Check OpenSSL library is installed in image
4. Ensure sufficient permissions for file creation

### If server won't start:
1. Check for port conflicts on 8140
2. Review error messages in server logs
3. Verify all shared libraries are available
4. Check SSL library compatibility

## Manual Testing Commands

```bash
# Full build and test sequence
docker-compose down -v  # Clean slate
docker-compose build server agent  # Build both images
docker-compose up -d server  # Start server
sleep 10  # Wait for initialization
docker-compose logs server  # Check CA initialization
docker-compose exec server ls -la /etc/puppetc/ssl/ca/  # Verify CA files
docker-compose exec server openssl x509 -in /etc/puppetc/ssl/ca/ca_crt.pem -text -noout  # Verify CA cert
curl -k https://localhost:8140/status  # Test HTTPS endpoint
```

## Notes

- This verification was performed in an automated environment where Docker commands are restricted
- All pre-build checks passed successfully
- Source files, Makefiles, Dockerfiles, and server code are correctly configured
- The actual Docker build and runtime verification must be performed in an environment with Docker access
- This document provides comprehensive instructions for manual verification
