# Proxy EAGAIN Fix and Full-Featured Proxy Loader

## Problem Fixed

### EAGAIN During SOCKS5 Proxy Negotiation

**Root Cause:**
- Sockets are set to non-blocking mode (O_NONBLOCK) in `new_clone()`
- Proxy negotiation functions called `read()` and `write()` directly without waiting for socket readiness
- This caused `EAGAIN` errors on non-blocking sockets during protocol handshakes

**Solution:**
Added two helper functions that properly handle non-blocking I/O:

1. **`safe_read_with_timeout()`**
   - Uses `select()` to wait for socket readability before calling `read()`
   - Handles partial reads in a loop
   - Treats `EAGAIN`/`EWOULDBLOCK` as transient (retries after `select()`)
   - Implements configurable timeout (default: 10 seconds)
   - Returns exact number of bytes requested or error

2. **`safe_write_with_timeout()`**
   - Uses `select()` to wait for socket writability before calling `write()`
   - Handles partial writes in a loop
   - Treats `EAGAIN`/`EWOULDBLOCK` as transient (retries after `select()`)
   - Implements configurable timeout (default: 10 seconds)
   - Returns exact number of bytes requested or error

### Updated Functions

All proxy negotiation functions now use the safe helpers:

- **`socks4_connect()`** - Uses `safe_read_with_timeout()` and `safe_write_with_timeout()`
- **`socks5_connect()`** - Uses `safe_read_with_timeout()` and `safe_write_with_timeout()`
- **`http_connect()`** - Uses `safe_read_with_timeout()` and `safe_write_with_timeout()`

### Connect Handling

The `connect_through_proxy()` function already properly handled non-blocking `connect()`:
- Calls `connect()` which returns `-1` with `errno = EINPROGRESS` on non-blocking sockets
- Uses `select()` to wait for writability (connection completion)
- Verifies connection success with `getsockopt(SOL_SOCKET, SO_ERROR)`

---

## Full-Featured Proxy Loader

### New Features

#### 1. Proxy Validation Command

**Usage:**
```
proxy check [--timeout <ms>] [--save <file>]
```

**Behavior:**
- Validates all loaded proxies by:
  1. Testing TCP connectivity (non-blocking connect + select)
  2. Performing protocol negotiation (SOCKS5, SOCKS4, HTTP)
  3. Removing unreachable/failed proxies
- Prints summary with counts per protocol
- Optionally saves validated proxies to file

#### 2. Load with Auto-Check

**Usage:**
```
proxy <filename> --check [--timeout <ms>] [--save <file>]
proxy <type> <filename> --check [--timeout <ms>] [--save <file>]
```

**Behavior:**
- Loads proxies from file
- Automatically validates them if `--check` is specified
- Removes failed proxies from in-memory list
- Optionally saves validated list

#### 3. Protocol Detection

When proxy type is `auto` or unspecified in file, the loader:
1. Tries SOCKS5 handshake first
2. Falls back to SOCKS4 if SOCKS5 fails
3. Falls back to HTTP if SOCKS4 fails
4. Marks proxy as failed if all protocols fail

#### 4. Supported Proxy Formats

All original formats are supported:
- `IP:PORT`
- `USERNAME:PASSWORD@IP:PORT`
- `IP:PORT:USERNAME:PASSWORD`
- `scheme://IP:PORT` (socks4://, socks5://, http://, https://)
- IPv6: `[IPv6]:PORT`, `[USERNAME:PASSWORD@[IPv6]:PORT]`

### Logging

The proxy loader provides detailed logging:

```
[INFO] Loading proxies from proxy.txt (100 entries)
[DEBUG] Checking 1/100 192.0.2.30:1080
[DEBUG] connect() in progress -> select() writable
[DEBUG] SOCKS5 greeting sent, awaiting method reply
[DEBUG] SOCKS5 method: NO_AUTH
[INFO] Proxy OK: 192.0.2.30:1080 -> SOCKS5 (no auth) (rtt=120ms)
[DEBUG] Checking 2/100 192.0.2.31:1080
[DEBUG] connect() timed out (3000ms)
[WARN] Proxy removed (connect timed out): 192.0.2.31:1080
...
[INFO] Summary: total=100, removed=72, working=28 (SOCKS5=20, SOCKS4=2, HTTP=6)
```

### Implementation Files

- **`proxy.c`** - Core proxy functionality with EAGAIN fixes
- **`proxy_loader.c`** - Proxy validation and loader implementation
- **`command.c`** - Updated proxy command handler
- **`command.h`** - Updated struct definitions and function declarations

---

## Testing

### Manual Test

1. Create a test proxy file:
```
# test_proxies.txt
socks5://192.0.2.1:1080
socks5://user:pass@192.0.2.2:1080
http://192.0.2.3:8080
[2001:db8::1]:1080
```

2. Load and validate:
```
./enemy
> .proxy test_proxies.txt --check --timeout 5000 --save validated.txt
```

3. Check results:
- Failed proxies are removed from memory
- Working proxies remain in the list
- Summary shows counts per protocol
- `validated.txt` contains only working proxies

### Expected Behavior

**Before Fix:**
```
[!] socks5_connect()->read(): Resource temporarily unavailable (proxy not responding or bad SOCKS5 server)
[!] Proxy negotiation failed for irc.server.com via 192.0.2.1:1080
```

**After Fix:**
```
[>] Proxy 192.0.2.1:1080 (SOCKS5) connected, negotiating tunnel to irc.server.com:6667
[>] Proxy 192.0.2.1:1080 (SOCKS5) is online and tunneling irc.server.com:6667
```

---

## Technical Details

### Non-Blocking Socket Flow

1. **Socket Creation** (in `new_clone()`)
   - `socket()` creates TCP socket
   - `fcntl(fd, F_SETFL, O_NONBLOCK)` sets non-blocking mode

2. **Connect** (in `connect_through_proxy()`)
   - `connect()` returns `-1` with `EINPROGRESS`
   - `select()` waits for writability (timeout: 30s)
   - `getsockopt(SO_ERROR)` verifies connection success

3. **Protocol Negotiation** (in `socks5_connect()`, etc.)
   - `safe_write_with_timeout()` sends handshake data
     - `select()` waits for writability
     - `write()` sends data (handles partial writes)
   - `safe_read_with_timeout()` receives response
     - `select()` waits for readability
     - `read()` receives data (handles partial reads)

### Timeout Configuration

Default timeouts (in seconds):
- **Connect timeout:** 30s (in `connect_through_proxy()`)
- **Read/Write timeout:** 10s (in `safe_read_with_timeout()`/`safe_write_with_timeout()`)
- **Proxy loader timeout:** 5000ms (configurable via `--timeout`)

### Error Handling

The implementation distinguishes between:
1. **Transient errors** (EAGAIN, EWOULDBLOCK, EINTR) - Retry after select()
2. **Timeout errors** (ETIMEDOUT) - Logged with timeout message
3. **Connection errors** (ECONNRESET, EOF) - Logged with appropriate message
4. **Protocol errors** - Logged with specific SOCKS/HTTP error codes

---

## Important Note

**If proxies still fail with EAGAIN, that means you need to add select() (or poll()/epoll()) before read() in the proxy functions.**

This implementation already does this correctly using the `safe_read_with_timeout()` and `safe_write_with_timeout()` helper functions.

---

## Files Modified

- `proxy.c` - Added safe I/O helpers and updated negotiation functions
- `proxy_loader.c` - New file with validation logic
- `command.c` - Updated proxy command handler
- `command.h` - Updated struct definitions
- `Makefile` - Added proxy_loader.o to build

---

## Commit Message

```
Fix EAGAIN during SOCKS5 proxy negotiation and implement full-featured proxy loader

- Add safe_read_with_timeout() and safe_write_with_timeout() helpers
- Use select() before read/write in all proxy negotiation functions
- Handle partial reads/writes in loops
- Treat EAGAIN/EWOULDBLOCK as transient errors
- Implement proxy validation with protocol detection
- Add proxy check command with timeout and save options
- Support all proxy formats: IP:PORT, user:pass@IP:PORT, schemes, IPv6
- Log detailed proxy check results with RTT and protocol info
- Remove failed proxies from in-memory list
- Save validated proxies to file with detected protocols
```
