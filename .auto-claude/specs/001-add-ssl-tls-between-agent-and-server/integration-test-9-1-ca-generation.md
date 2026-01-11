# Integration Test: Server CA Generation on First Startup (subtask-9-1)

## Test Objective
Verify that the puppetc-server generates a Certificate Authority (CA) on first startup when no existing CA is found.

## Test Type
End-to-End Integration Test

## Prerequisites
- Docker and docker-compose installed
- Server Docker image built successfully
- No existing CA in the server SSL volume

## Test Steps

### 1. Clean Environment Setup
```bash
# Remove any existing volumes to ensure clean state
docker-compose down -v

# Remove server SSL volume specifically
docker volume rm puppetc-server-ssl 2>/dev/null || true
```

**Expected:** Clean slate with no existing certificates

### 2. Start Server Container
```bash
# Start server in detached mode
docker-compose up -d server

# Wait for server initialization
sleep 10
```

**Expected:** Server container starts successfully

### 3. Check Server Logs for CA Generation
```bash
# View server logs for CA initialization messages
docker-compose logs server | grep -i "ca"
```

**Expected output patterns:**
```
[INFO] Initializing CA at /etc/puppetc/ssl/ca
[INFO] CA not found, generating new CA...
[INFO] Generating CA certificate with subject: CN=Puppet CA: puppetc-server
[INFO] CA generated successfully
[INFO] CA saved to /etc/puppetc/ssl/ca/ca_crt.pem
```

OR if verbose mode enabled:
```
Verbose: CA directory: /etc/puppetc/ssl/ca
Verbose: CA does not exist, generating...
Verbose: CA subject: CN=Puppet CA: puppetc-server
Verbose: CA validity: 3650 days
Verbose: CA generation completed
```

### 4. Verify CA Certificate File Exists
```bash
# Check that CA certificate file exists
docker-compose exec server ls -la /etc/puppetc/ssl/ca/ca_crt.pem
```

**Expected output:**
```
-rw-r--r-- 1 root root <size> <date> /etc/puppetc/ssl/ca/ca_crt.pem
```

**File permission requirements:**
- Permissions: 0644 (readable by all, writable by owner only)
- Owner: root
- File should be approximately 1200-1400 bytes for a 2048-bit RSA certificate

### 5. Verify CA Private Key File Exists
```bash
# Check that CA private key exists with secure permissions
docker-compose exec server ls -la /etc/puppetc/ssl/ca/ca_key.pem
```

**Expected output:**
```
-rw------- 1 root root <size> <date> /etc/puppetc/ssl/ca/ca_key.pem
```

**File permission requirements:**
- Permissions: 0600 (readable/writable by owner only) - CRITICAL for security
- Owner: root
- File should be approximately 1700-1900 bytes for a 2048-bit RSA private key

### 6. Verify CA Certificate is Valid X.509
```bash
# Examine CA certificate with OpenSSL
docker-compose exec server openssl x509 -in /etc/puppetc/ssl/ca/ca_crt.pem -text -noout
```

**Expected certificate details:**

#### Subject Information
- **Subject:** `CN=Puppet CA: puppetc-server`
- Must match the server's CA subject pattern

#### Validity Period
- **Not Before:** Current date/time or slightly before
- **Not After:** 10 years from generation (3650 days)
- **Validity check:** Certificate should be currently valid

#### Public Key Information
- **Public Key Algorithm:** rsaEncryption
- **RSA Public-Key:** 2048 bit or greater
- Modulus and exponent should be present

#### Signature
- **Signature Algorithm:** sha256WithRSAEncryption (or stronger)
- NOT md5 or sha1 (weak algorithms)

#### X.509v3 Extensions (Critical for CA)
```
X.509v3 extensions:
    X.509v3 Basic Constraints: critical
        CA:TRUE
    X.509v3 Key Usage: critical
        Certificate Sign, CRL Sign
    X.509v3 Subject Key Identifier:
        <hex value>
```

**Critical validations:**
- `CA:TRUE` must be present (indicates this is a Certificate Authority)
- Key Usage must include "Certificate Sign" (allows signing other certificates)
- Subject Key Identifier should be present for proper certificate chain validation

### 7. Verify Serial Number File Initialization
```bash
# Check that serial number file exists for certificate issuance
docker-compose exec server cat /etc/puppetc/ssl/ca/serial
```

**Expected output:**
```
01
```

OR
```
1000
```

**Requirements:**
- File should contain an initial serial number (typically "01" or "1000")
- This number will increment with each certificate signed

### 8. Verify Signed Certificates Directory
```bash
# Check that directory for signed certificates exists
docker-compose exec server ls -ld /etc/puppetc/ssl/ca/signed/
```

**Expected output:**
```
drwxr-xr-x 2 root root <size> <date> /etc/puppetc/ssl/ca/signed/
```

**Requirements:**
- Directory should exist (may be empty initially)
- Permissions: 0755 (readable by all, writable by owner)

## Code Verification

### Server CA Initialization Code (puppetc_server.c)
The following code sections implement CA generation on first startup:

**Location:** `server/puppetc_server.c`, lines 1824-1859

```c
/* Initialize CA */
ca_ctx = puppet_ca_init(ca_dir);
if (!ca_ctx) {
    fprintf(stderr, "Warning: Failed to initialize CA at %s\n", ca_dir);
}

if (ca_ctx) {
    if (verbose) {
        printf("Verbose: CA directory: %s\n", ca_dir);
    }
    /* Check if CA exists, if not generate it */
    if (!puppet_ca_exists(ca_ctx)) {
        if (verbose) {
            printf("Verbose: CA does not exist, generating...\n");
        }
        char ca_subject[256];
        snprintf(ca_subject, sizeof(ca_subject), "CN=Puppet CA: puppetc-server");
        if (puppet_ca_generate(ca_ctx, ca_subject, PUPPET_CA_VALIDITY_DAYS) != 0) {
            fprintf(stderr, "Warning: Failed to generate CA: %s\n",
                    puppet_ca_get_error(ca_ctx));
        } else {
            if (verbose) {
                printf("Verbose: CA generated successfully\n");
            }
            if (puppet_ca_save(ca_ctx) != 0) {
                fprintf(stderr, "Warning: Failed to save CA: %s\n",
                        puppet_ca_get_error(ca_ctx));
            }
        }
    } else {
        /* Load existing CA */
        if (puppet_ca_load(ca_ctx) != 0) {
            fprintf(stderr, "Warning: Failed to load CA: %s\n",
                    puppet_ca_get_error(ca_ctx));
        }
    }
}
```

**Key functions verified:**
- ✓ `puppet_ca_init(ca_dir)` - Initializes CA context
- ✓ `puppet_ca_exists(ca_ctx)` - Checks if CA already exists
- ✓ `puppet_ca_generate(ca_ctx, subject, validity)` - Generates new CA
- ✓ `puppet_ca_save(ca_ctx)` - Saves CA to disk
- ✓ `puppet_ca_load(ca_ctx)` - Loads existing CA
- ✓ `puppet_ca_get_error(ca_ctx)` - Error reporting

## Success Criteria

All of the following must be true:

- [x] **Code Review:** CA initialization code present in puppetc_server.c (verified at lines 1824-1859)
- [ ] **Server Startup:** Server starts successfully without errors
- [ ] **Log Messages:** Server logs show CA generation messages
- [ ] **CA Certificate File:** `/etc/puppetc/ssl/ca/ca_crt.pem` exists with 0644 permissions
- [ ] **CA Private Key File:** `/etc/puppetc/ssl/ca/ca_key.pem` exists with 0600 permissions (CRITICAL)
- [ ] **X.509 Validation:** Certificate is valid X.509 format with CA:TRUE
- [ ] **Certificate Subject:** Subject is "CN=Puppet CA: puppetc-server"
- [ ] **Key Size:** RSA key is 2048 bits or greater
- [ ] **Signature Algorithm:** Uses SHA-256 or stronger (NOT MD5 or SHA-1)
- [ ] **Validity Period:** Certificate valid for 10 years (3650 days)
- [ ] **Serial File:** Serial number file initialized
- [ ] **Signed Directory:** Directory for signed certificates exists

## Automated Verification Script

For convenience, here's a complete test script:

```bash
#!/bin/bash
set -e

echo "=== Integration Test 9-1: Server CA Generation on First Startup ==="
echo

# Step 1: Clean environment
echo "Step 1: Cleaning environment..."
docker-compose down -v
docker volume rm puppetc-server-ssl 2>/dev/null || true
echo "✓ Environment cleaned"
echo

# Step 2: Start server
echo "Step 2: Starting server..."
docker-compose up -d server
sleep 10
echo "✓ Server started"
echo

# Step 3: Check logs
echo "Step 3: Checking server logs for CA generation..."
docker-compose logs server | grep -i "ca" | head -20
echo

# Step 4: Verify CA certificate file
echo "Step 4: Verifying CA certificate file..."
docker-compose exec server ls -la /etc/puppetc/ssl/ca/ca_crt.pem
CA_CERT_PERMS=$(docker-compose exec server stat -c "%a" /etc/puppetc/ssl/ca/ca_crt.pem)
if [ "$CA_CERT_PERMS" = "644" ]; then
    echo "✓ CA certificate has correct permissions (644)"
else
    echo "✗ CA certificate has incorrect permissions: $CA_CERT_PERMS (expected 644)"
    exit 1
fi
echo

# Step 5: Verify CA private key file
echo "Step 5: Verifying CA private key file..."
docker-compose exec server ls -la /etc/puppetc/ssl/ca/ca_key.pem
CA_KEY_PERMS=$(docker-compose exec server stat -c "%a" /etc/puppetc/ssl/ca/ca_key.pem)
if [ "$CA_KEY_PERMS" = "600" ]; then
    echo "✓ CA private key has correct permissions (600)"
else
    echo "✗ CA private key has incorrect permissions: $CA_KEY_PERMS (expected 600)"
    exit 1
fi
echo

# Step 6: Verify X.509 certificate
echo "Step 6: Verifying CA certificate is valid X.509..."
docker-compose exec server openssl x509 -in /etc/puppetc/ssl/ca/ca_crt.pem -text -noout > /tmp/ca_cert.txt

echo "Certificate Subject:"
grep "Subject:" /tmp/ca_cert.txt

echo "Certificate Validity:"
grep "Not Before\|Not After" /tmp/ca_cert.txt

echo "Public Key:"
grep "Public-Key\|RSA" /tmp/ca_cert.txt | head -2

echo "Signature Algorithm:"
grep "Signature Algorithm" /tmp/ca_cert.txt | head -1

echo "CA Extension:"
grep -A1 "Basic Constraints" /tmp/ca_cert.txt

# Validate CA:TRUE is present
if grep -q "CA:TRUE" /tmp/ca_cert.txt; then
    echo "✓ Certificate has CA:TRUE extension"
else
    echo "✗ Certificate missing CA:TRUE extension"
    exit 1
fi

# Validate strong signature algorithm
if grep -q "sha256\|sha384\|sha512" /tmp/ca_cert.txt; then
    echo "✓ Certificate uses strong signature algorithm"
else
    echo "✗ Certificate uses weak signature algorithm"
    exit 1
fi
echo

# Step 7: Verify serial file
echo "Step 7: Verifying serial number file..."
docker-compose exec server cat /etc/puppetc/ssl/ca/serial
echo "✓ Serial file initialized"
echo

# Step 8: Verify signed directory
echo "Step 8: Verifying signed certificates directory..."
docker-compose exec server ls -ld /etc/puppetc/ssl/ca/signed/
echo "✓ Signed certificates directory exists"
echo

echo "=== All Integration Tests Passed! ==="
```

## Environment Limitations

**Note:** This test document was created in an automated environment where Docker commands are not available for execution. The verification steps are provided for manual execution in a Docker-enabled environment.

### Pre-Verification Completed
The following items have been verified in the source code:
- ✓ CA initialization code exists in `server/puppetc_server.c` (lines 1824-1859)
- ✓ CA implementation in `server/puppet_ca.c` (32KB file, complete implementation)
- ✓ SSL utilities in `common/puppet_ssl.c` (15KB file)
- ✓ Header files present: `include/puppet_ca.h`, `include/puppet_ssl.h`
- ✓ Dockerfile includes OpenSSL tools and SSL directories
- ✓ docker-compose.yml has SSL volumes configured

### Manual Verification Required
The following runtime verification steps require a Docker-enabled environment:
- [ ] Execute test script to verify CA generation at runtime
- [ ] Confirm CA certificate file creation
- [ ] Validate X.509 certificate structure and extensions
- [ ] Verify file permissions for security compliance

## Related Documentation
- Main Docker verification: `docker-build-verification.md`
- Specification: `spec.md`
- Implementation plan: `implementation_plan.json`

## References
- OpenSSL x509 man page: https://www.openssl.org/docs/man3.0/man1/openssl-x509.html
- X.509 Certificate Format: https://datatracker.ietf.org/doc/html/rfc5280
- Puppet CA Documentation: https://puppet.com/docs/puppet/latest/ssl_certificates.html
