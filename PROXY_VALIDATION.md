# Proxy Validation and Protocol Detection

## Overview

The proxy loader now automatically validates proxies after loading, with concurrent checking and protocol detection support for SOCKS5, SOCKS4, and HTTP/HTTPS proxies.

## Features

### Automatic Validation
- By default, proxies are automatically validated after loading
- Failed proxies are removed from the list
- Only working proxies remain for round-robin selection

### Protocol Detection
- Automatically detects SOCKS5, SOCKS4, and HTTP/HTTPS protocols
- If a protocol type is specified during load (e.g., `proxy socks5 file.txt`), it will be tested first
- Falls back to auto-detection if the specified protocol fails

### Concurrent Checking
- Multiple proxies are checked simultaneously (default: 10 concurrent connections)
- Configurable concurrency: `--concurrency N` (1-100)
- Significantly faster than serial checking for large proxy lists

### Timeout Configuration
- Configurable timeout for connect and handshake: `--timeout MS` (100-60000ms)
- Default: 5000ms (5 seconds)

### Round-Robin Behavior
- `next_proxy()` function now only returns validated, active proxies
- Skips failed/removed proxies automatically
- Maintains round-robin rotation through working proxies only

## Commands

### Load Proxies with Validation
```
.proxy <filename>
.proxy <filename> --concurrency 10 --timeout 5000
.proxy socks5 <filename>
.proxy <filename> --no-check        # Skip validation
.proxy <filename> --save validated.txt
```

### Re-validate Existing Proxies
```
.proxy check
.proxy check --concurrency 20 --timeout 3000
.proxy check --save working.txt
```

### View Proxy Status
```
.proxy
```
Shows:
- Number of loaded proxies
- Source file
- Default type (or Auto-detect)
- Number of validated proxies by protocol

### Clear Proxies
```
.proxy clear
```

## Command Options

| Option | Description | Default | Range |
|--------|-------------|---------|-------|
| `--check` | Validate proxies after loading | ON | - |
| `--no-check` | Skip validation | OFF | - |
| `--concurrency N` | Number of concurrent checks | 10 | 1-100 |
| `--timeout MS` | Connect/handshake timeout (ms) | 5000 | 100-60000 |
| `--save <file>` | Save validated proxies to file | - | - |
| `--test-host <host>` | Test destination host | irc.libera.chat | - |
| `--test-port <port>` | Test destination port | 6667 | 1-65535 |

## Proxy File Formats

Supports multiple formats:

```
# IPv4 with port
8.8.8.8:1080

# IPv6 with port
[2001:db8::1]:1080

# With protocol
socks5://proxy.example.com:1080
socks4://proxy.example.com:1080
http://proxy.example.com:8080

# With authentication (prefix notation)
socks5://user:pass@proxy.example.com:1080

# With authentication (suffix notation)
proxy.example.com:1080:user:pass
```

## Validation Process

For each proxy:

1. **TCP Connect Test**
   - Attempts non-blocking TCP connection to proxy
   - Uses poll() for timeout handling
   - Records RTT (Round Trip Time)

2. **Protocol Negotiation**
   - If protocol specified: Tests that protocol only
   - If auto-detect: Tests SOCKS5, then SOCKS4, then HTTP
   - For SOCKS5: Performs full handshake including auth if credentials present
   - For SOCKS4: Sends CONNECT request
   - For HTTP: Sends CONNECT method request

3. **Result**
   - **Success**: Proxy marked as validated, active, protocol type set
   - **Failure**: Proxy removed from list with detailed error message

## Output Messages

### Success
```
[>] Proxy OK: 1.2.3.4:1080 -> SOCKS5 (auth) (connect=150ms total=300ms)
```

### Failure Examples
```
[!] Proxy removed: 1.2.3.4:1080 (connect timeout, total=5002ms)
[!] Proxy removed: 1.2.3.4:1080 (connect(): Connection refused, total=50ms)
[!] Proxy removed: 1.2.3.4:1080 (SOCKS5 negotiation failed, total=250ms)
```

### Summary
```
[>] Summary: total=100, removed=45, working=55 (SOCKS5=30, SOCKS4=10, HTTP=15)
```

## Persistent Validation State

Proxy struct fields:
- `validated`: 1 if proxy passed validation
- `is_active`: 1 if proxy is currently usable
- `detected_type`: Detected protocol (SOCKS5, SOCKS4, HTTP, etc.)
- `has_auth`: 1 if proxy has username/password
- `last_rtt_ms`: Last recorded RTT in milliseconds

## Performance

- **Serial** (concurrency=1): ~5 seconds per proxy (with 5s timeout)
- **Concurrent** (concurrency=10): ~5 seconds for 10 proxies simultaneously
- **Concurrent** (concurrency=50): ~5 seconds for 50 proxies simultaneously

For 1000 proxies with 5s timeout:
- Serial: ~5000 seconds (~83 minutes)
- Concurrency=10: ~500 seconds (~8 minutes)
- Concurrency=50: ~100 seconds (~1.7 minutes)

## Thread Safety

The validation uses pthreads with proper locking:
- `index_lock`: Protects the next proxy index to test
- `stats_lock`: Protects counters (working, removed, protocol counts)
- `log_lock`: Protects console output to prevent interleaved messages

## Integration with load Command

When using `.load` to connect IRC clones:
- Only validated, active proxies are used
- Round-robin skips any failed proxies
- If a proxy fails during actual use, it can be marked inactive for future connections
