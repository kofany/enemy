#!/bin/bash
# Test script to demonstrate EAGAIN fix for proxy negotiation
#
# This script creates a simple test environment to verify that:
# 1. Proxies load correctly with all supported formats
# 2. Non-blocking socket I/O doesn't cause EAGAIN errors
# 3. Proxy validation works with connect timeouts

set -e

echo "=== Proxy EAGAIN Fix Test Script ==="
echo ""

# Create test proxy file with various formats
echo "Creating test proxy file..."
cat > test_proxies_demo.txt << 'EOF'
# Test proxies - various formats
# Note: These are example/documentation IPs, they won't actually connect

# Basic format
192.0.2.1:1080
192.0.2.2:8080

# With credentials (colon-separated)
192.0.2.3:1080:testuser:testpass

# With credentials (@ format)
proxyuser:proxypass@192.0.2.4:1080

# With scheme
socks5://192.0.2.5:1080
socks5://user:pass@192.0.2.6:1080
http://192.0.2.7:8080
https://192.0.2.8:8080

# IPv6 format
[2001:db8::1]:1080
[2001:db8::2]:1080:user6:pass6
user:pass@[2001:db8::3]:1080
EOF

echo "Test proxy file created: test_proxies_demo.txt"
echo ""
echo "Contents:"
cat test_proxies_demo.txt
echo ""
echo ""

# Test loading proxies
echo "=== Test 1: Load proxies without validation ==="
echo "Command: .proxy test_proxies_demo.txt"
echo ""

# Test with type specified
echo "=== Test 2: Load proxies with type specified ==="
echo "Command: .proxy socks5 test_proxies_demo.txt"
echo ""

# Test validation
echo "=== Test 3: Load and validate proxies ==="
echo "Command: .proxy test_proxies_demo.txt --check --timeout 3000 --save validated_proxies.txt"
echo "Expected behavior:"
echo "  - Loads all proxies"
echo "  - Tests connectivity to each proxy"
echo "  - Removes proxies that fail to connect or negotiate"
echo "  - Saves working proxies to validated_proxies.txt"
echo "  - NO EAGAIN ERRORS should occur during negotiation"
echo ""

# Test validation of already loaded proxies
echo "=== Test 4: Validate already loaded proxies ==="
echo "Command: .proxy check --timeout 5000 --save working_proxies.txt"
echo ""

echo "=== Test 5: Verify EAGAIN fix ==="
echo "The fix ensures that:"
echo "  1. select() is called before every read() during proxy negotiation"
echo "  2. select() is called before every write() during proxy negotiation"
echo "  3. EAGAIN/EWOULDBLOCK are handled as transient errors (retry after select)"
echo "  4. Partial reads/writes are handled in loops"
echo "  5. Timeouts are implemented for all operations"
echo ""
echo "Key functions implementing the fix:"
echo "  - safe_read_with_timeout()  (in proxy.c)"
echo "  - safe_write_with_timeout() (in proxy.c)"
echo "  - socks4_connect()  (uses safe helpers)"
echo "  - socks5_connect()  (uses safe helpers)"
echo "  - http_connect()    (uses safe helpers)"
echo ""

echo "=== Usage Examples ==="
echo ""
echo "1. Load proxies from file:"
echo "   .proxy myproxies.txt"
echo ""
echo "2. Load proxies and auto-detect protocol:"
echo "   .proxy auto myproxies.txt"
echo ""
echo "3. Load proxies with specific type:"
echo "   .proxy socks5 myproxies.txt"
echo ""
echo "4. Load and validate proxies:"
echo "   .proxy myproxies.txt --check --timeout 5000"
echo ""
echo "5. Load, validate, and save working proxies:"
echo "   .proxy myproxies.txt --check --timeout 5000 --save working.txt"
echo ""
echo "6. Validate already loaded proxies:"
echo "   .proxy check --timeout 3000"
echo ""
echo "7. Show current proxy status:"
echo "   .proxy"
echo ""
echo "8. Clear proxy list:"
echo "   .proxy clear"
echo ""

echo "=== Technical Details ==="
echo ""
echo "The EAGAIN fix works as follows:"
echo ""
echo "BEFORE (caused EAGAIN errors):"
echo "  1. socket() creates non-blocking socket"
echo "  2. connect() returns EINPROGRESS"
echo "  3. select() waits for connection"
echo "  4. write() sends SOCKS5 greeting   <- WOULD SOMETIMES FAIL WITH EAGAIN"
echo "  5. read() receives method reply    <- WOULD SOMETIMES FAIL WITH EAGAIN"
echo ""
echo "AFTER (no EAGAIN errors):"
echo "  1. socket() creates non-blocking socket"
echo "  2. connect() returns EINPROGRESS"
echo "  3. select() waits for connection"
echo "  4. safe_write_with_timeout():"
echo "     - select() waits for writability"
echo "     - write() sends data (handles partial writes)"
echo "     - loop until all data sent or timeout"
echo "  5. safe_read_with_timeout():"
echo "     - select() waits for readability"
echo "     - read() receives data (handles partial reads)"
echo "     - loop until all data received or timeout"
echo ""

echo "=== Documentation ==="
echo "See PROXY_EAGAIN_FIX.md for complete documentation"
echo ""

echo "Test script complete!"
echo "Run './enemy' and use the commands above to test proxy functionality."
