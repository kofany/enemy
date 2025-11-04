/* Proxy support implementation
 * Supports HTTP, HTTPS, SOCKS4, SOCKS5 proxies
 * IPv4 and IPv6 support
 * Open and password-protected proxies
 *
 * IMPORTANT NOTE ABOUT EAGAIN:
 * If proxies still fail with EAGAIN, that means you need to add select()
 * (or poll()/epoll()) before read() in the proxy functions.
 *
 * This implementation uses safe_read_with_timeout() and safe_write_with_timeout()
 * which properly handle non-blocking sockets by:
 * - Using select() to wait for socket readiness before read/write
 * - Handling partial reads/writes in loops
 * - Treating EAGAIN/EWOULDBLOCK as transient (retry after select())
 * - Implementing configurable timeouts for all operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include "defs.h"
#include "main.h"
#include "command.h"

#define PROXY_DEFAULT_CONNECT_TIMEOUT_MS 7000
#define PROXY_DEFAULT_HANDSHAKE_TIMEOUT_MS 7000
#define PROXY_MIN_TIMEOUT_MS 100
#define PROXY_MAX_TIMEOUT_MS 60000
#define PROXY_STAGE(stage) ((stage) ? (stage) : "unknown")

static long long proxy_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

static int clamp_timeout_ms(int timeout_ms, int fallback_ms)
{
    if (timeout_ms <= 0)
        timeout_ms = fallback_ms;
    if (timeout_ms < PROXY_MIN_TIMEOUT_MS)
        timeout_ms = PROXY_MIN_TIMEOUT_MS;
    if (timeout_ms > PROXY_MAX_TIMEOUT_MS)
        timeout_ms = PROXY_MAX_TIMEOUT_MS;
    return timeout_ms;
}

static int proxy_connect_timeout_ms_value(void)
{
    return clamp_timeout_ms(xconnect.proxy_connect_timeout_ms, PROXY_DEFAULT_CONNECT_TIMEOUT_MS);
}

static int proxy_handshake_timeout_ms_value(void)
{
    return clamp_timeout_ms(xconnect.proxy_handshake_timeout_ms, PROXY_DEFAULT_HANDSHAKE_TIMEOUT_MS);
}

static char *trim_whitespace(char *s)
{
    char *end;

    if (!s)
        return s;

    while (*s && isspace((unsigned char)*s))
        s++;

    if (*s == '\0')
        return s;

    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        *end-- = '\0';
    if (isspace((unsigned char)*end))
        *end = '\0';

    return s;
}

proxy *parse_proxy_line(char *line, enum proxy_type default_type)
{
    proxy *p;
    char buffer[MAX_PROXY_LINE];
    char *work;
    enum proxy_type ptype = default_type;
    int is_ipv6 = 0;
    char *scheme_end;
    char *prefix_user = NULL, *prefix_pass = NULL;
    char *suffix_user = NULL, *suffix_pass = NULL;
    char *port_token = NULL;
    char *host_token = NULL;

    if (!line || !*line)
        return NULL;

    strncpy(buffer, line, MAX_PROXY_LINE - 1);
    buffer[MAX_PROXY_LINE - 1] = '\0';

    for (char *s = buffer; *s; s++)
        if (*s == '\n' || *s == '\r')
            *s = '\0';

    work = trim_whitespace(buffer);
    if (!*work || *work == '#')
        return NULL;

    size_t wlen = strlen(work);
    if (wlen > 2 && work[0] == '[' && work[wlen - 1] == ']' && strchr(work + 1, '@')) {
        work[wlen - 1] = '\0';
        work = trim_whitespace(work + 1);
        if (!*work)
            return NULL;
    }

    p = (proxy *)calloc(1, sizeof(proxy));
    if (!p) {
        err_printf("parse_proxy_line()->calloc(): %s\n", strerror(errno));
        return NULL;
    }

    scheme_end = strstr(work, "://");
    if (scheme_end) {
        *scheme_end = '\0';
        if (!x_strcasecmp(work, "http"))
            ptype = PROXY_HTTP;
        else if (!x_strcasecmp(work, "https"))
            ptype = PROXY_HTTPS;
        else if (!x_strcasecmp(work, "socks4"))
            ptype = PROXY_SOCKS4;
        else if (!x_strcasecmp(work, "socks5"))
            ptype = PROXY_SOCKS5;
        work = trim_whitespace(scheme_end + 3);
    }
    p->type = ptype;

    char *at_sign = strrchr(work, '@');
    if (at_sign) {
        *at_sign = '\0';
        char *user_pass = trim_whitespace(work);
        work = trim_whitespace(at_sign + 1);

        if (*user_pass) {
            char *pass_sep = strchr(user_pass, ':');
            if (pass_sep) {
                *pass_sep = '\0';
                prefix_user = trim_whitespace(user_pass);
                prefix_pass = trim_whitespace(pass_sep + 1);
            } else {
                prefix_user = user_pass;
            }
        }
    } else {
        work = trim_whitespace(work);
    }

    if (!*work) {
        del_proxy(p);
        return NULL;
    }

    if (*work == '[') {
        char *closing = strchr(work, ']');
        if (!closing) {
            del_proxy(p);
            return NULL;
        }
        *closing = '\0';
        host_token = trim_whitespace(work + 1);
        char *rest = trim_whitespace(closing + 1);
        if (*rest == ':') {
            rest = trim_whitespace(rest + 1);
        } else if (*rest != '\0') {
            del_proxy(p);
            return NULL;
        } else {
            del_proxy(p);
            return NULL;
        }

        if (!*rest) {
            del_proxy(p);
            return NULL;
        }

        char *after_port = strchr(rest, ':');
        if (after_port) {
            *after_port = '\0';
            suffix_user = trim_whitespace(after_port + 1);
            if (suffix_user && *suffix_user) {
                char *after_user = strchr(suffix_user, ':');
                if (after_user) {
                    *after_user = '\0';
                    suffix_pass = trim_whitespace(after_user + 1);
                }
            }
        }

        port_token = trim_whitespace(rest);
        is_ipv6 = 1;
    } else {
        char *tmp = work;
        char *parts[4] = {0};
        int part_count = 0;

        while (tmp && part_count < 4) {
            parts[part_count++] = tmp;
            char *colon = strchr(tmp, ':');
            if (!colon) {
                tmp = NULL;
                break;
            }
            *colon = '\0';
            tmp = colon + 1;
        }

        if (tmp && part_count == 4 && *tmp) {
            tmp = trim_whitespace(tmp);
            if (*tmp) {
                size_t len = strlen(parts[3]);
                parts[3][len] = ':';
                strcpy(parts[3] + len + 1, tmp);
            }
        }

        if (part_count < 2) {
            del_proxy(p);
            return NULL;
        }

        host_token = trim_whitespace(parts[0]);
        port_token = trim_whitespace(parts[1]);

        if (!*host_token || !*port_token) {
            del_proxy(p);
            return NULL;
        }

        if (part_count >= 3)
            suffix_user = trim_whitespace(parts[2]);
        if (part_count >= 4)
            suffix_pass = trim_whitespace(parts[3]);
    }

    if (!host_token || !*host_token || !port_token || !*port_token) {
        del_proxy(p);
        return NULL;
    }

    p->host = strdup(host_token);
    if (!p->host) {
        err_printf("parse_proxy_line()->strdup(host): %s\n", strerror(errno));
        del_proxy(p);
        return NULL;
    }

    char *endptr;
    long port = strtol(port_token, &endptr, 10);
    while (*endptr && isspace((unsigned char)*endptr))
        endptr++;
    if (*endptr != '\0' || port <= 0 || port > 65535) {
        del_proxy(p);
        return NULL;
    }
    p->port = (int)port;

    if (prefix_user && *prefix_user) {
        p->username = strdup(prefix_user);
        if (!p->username) {
            err_printf("parse_proxy_line()->strdup(user): %s\n", strerror(errno));
            del_proxy(p);
            return NULL;
        }
    }

    if (prefix_pass && *prefix_pass) {
        p->password = strdup(prefix_pass);
        if (!p->password) {
            err_printf("parse_proxy_line()->strdup(pass): %s\n", strerror(errno));
            del_proxy(p);
            return NULL;
        }
    }

    if (!p->username && suffix_user && *suffix_user) {
        p->username = strdup(suffix_user);
        if (!p->username) {
            err_printf("parse_proxy_line()->strdup(user): %s\n", strerror(errno));
            del_proxy(p);
            return NULL;
        }
    }

    if (!p->password && suffix_pass && *suffix_pass) {
        p->password = strdup(suffix_pass);
        if (!p->password) {
            err_printf("parse_proxy_line()->strdup(pass): %s\n", strerror(errno));
            del_proxy(p);
            return NULL;
        }
    }

    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = is_ipv6 ? AF_INET6 : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", p->port);

    if (getaddrinfo(p->host, port_str, &hints, &result) == 0 && result) {
        if (result->ai_family == AF_INET6) {
            memcpy(&p->addr6, result->ai_addr, sizeof(struct sockaddr_in6));
            p->is_ipv6 = 1;
        } else if (result->ai_family == AF_INET) {
            memcpy(&p->addr, result->ai_addr, sizeof(struct sockaddr_in));
            p->is_ipv6 = 0;
        } else {
            freeaddrinfo(result);
            del_proxy(p);
            return NULL;
        }
        freeaddrinfo(result);
    } else {
        if (result)
            freeaddrinfo(result);
        del_proxy(p);
        return NULL;
    }

    return p;
}

int load_proxies(const char *filename, enum proxy_type default_type)
{
    FILE *fp;
    char line[MAX_PROXY_LINE];
    int count = 0;

    fp = fopen(filename, "r");
    if (!fp) {
        err_printf("load_proxies()->fopen(%s): %s\n", filename, strerror(errno));
        return -1;
    }

    del_proxy_all();

    while (fgets(line, sizeof(line), fp)) {
        proxy *p = parse_proxy_line(line, default_type);
        if (p) {
            if (xconnect.proxy_list == NULL) {
                xconnect.proxy_list = p;
                xconnect.proxy_tail = p;
            } else {
                xconnect.proxy_tail->next = p;
                p->parent = xconnect.proxy_tail;
                xconnect.proxy_tail = p;
            }
            count++;
        }
    }

    fclose(fp);
    xconnect.proxy_count = count;

    if (count > 0) {
        if (xconnect.proxy_file) {
            free(xconnect.proxy_file);
        }
        xconnect.proxy_file = strdup(filename);
        xconnect.proxy_default_type = default_type;
        xconnect.current_proxy = NULL;
        cinfo_printf("Loaded %d proxies from %s\n", count, filename);
        return count;
    } else {
        err_printf("No valid proxies found in %s\n", filename);
        return 0;
    }
}

void del_proxy(proxy *p)
{
    if (!p)
        return;

    if (p->host)
        free(p->host);
    if (p->username)
        free(p->username);
    if (p->password)
        free(p->password);
    free(p);
}

void del_proxy_all(void)
{
    proxy *p, *next;
    for (p = xconnect.proxy_list; p; p = next) {
        next = p->next;
        del_proxy(p);
    }
    xconnect.proxy_list = NULL;
    xconnect.proxy_tail = NULL;
    xconnect.current_proxy = NULL;
    xconnect.proxy_count = 0;
}

proxy *next_proxy(void)
{
    if (!xconnect.proxy_list)
        return NULL;

    if (!xconnect.current_proxy || !xconnect.current_proxy->next)
        xconnect.current_proxy = xconnect.proxy_list;
    else
        xconnect.current_proxy = xconnect.current_proxy->next;

    return xconnect.current_proxy;
}

static ssize_t safe_read_with_timeout(int sockfd, void *buf, size_t count, int timeout_sec)
{
    fd_set rfds;
    struct timeval tv;
    ssize_t total = 0;
    ssize_t n;
    char *ptr = (char *)buf;

    while (total < (ssize_t)count) {
        FD_ZERO(&rfds);
        FD_SET(sockfd, &rfds);
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;

        int ret = select(sockfd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (ret == 0) {
            errno = ETIMEDOUT;
            return -1;
        }

        n = read(sockfd, ptr + total, count - total);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return -1;
        }
        if (n == 0) {
            errno = ECONNRESET;
            return total > 0 ? total : -1;
        }
        total += n;
    }
    return total;
}

static ssize_t safe_write_with_timeout(int sockfd, const void *buf, size_t count, int timeout_sec)
{
    fd_set wfds;
    struct timeval tv;
    ssize_t total = 0;
    ssize_t n;
    const char *ptr = (const char *)buf;

    while (total < (ssize_t)count) {
        FD_ZERO(&wfds);
        FD_SET(sockfd, &wfds);
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;

        int ret = select(sockfd + 1, NULL, &wfds, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (ret == 0) {
            errno = ETIMEDOUT;
            return -1;
        }

        n = write(sockfd, ptr + total, count - total);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return -1;
        }
        if (n == 0) {
            errno = ECONNRESET;
            return -1;
        }
        total += n;
    }
    return total;
}

int socks4_connect(int sockfd, const char *dest_host, int dest_port, const char *userid)
{
    unsigned char buf[512];
    struct hostent *host;
    struct in_addr dest_addr;
    int len;

    if (inet_pton(AF_INET, dest_host, &dest_addr) != 1) {
        host = gethostbyname(dest_host);
        if (!host) {
            err_printf("socks4_connect()->gethostbyname(%s): %s\n", dest_host, hstrerror(h_errno));
            return -1;
        }
        memcpy(&dest_addr, host->h_addr, sizeof(dest_addr));
    }

    buf[0] = 4;
    buf[1] = 1;
    buf[2] = (dest_port >> 8) & 0xFF;
    buf[3] = dest_port & 0xFF;
    memcpy(buf + 4, &dest_addr, 4);

    len = 8;
    if (userid && *userid) {
        strncpy((char *)buf + len, userid, sizeof(buf) - len - 1);
        len += strlen(userid);
    }
    buf[len++] = 0;

    ssize_t written = safe_write_with_timeout(sockfd, buf, len, 10);
    if (written != len) {
        if (errno == ETIMEDOUT) {
            err_printf("socks4_connect()->write(): timeout (proxy not responding)\n");
        } else {
            err_printf("socks4_connect()->write(): %s (proxy disconnected)\n", strerror(errno));
        }
        return -1;
    }

    ssize_t n = safe_read_with_timeout(sockfd, buf, 8, 10);
    if (n != 8) {
        if (n < 0) {
            if (errno == ETIMEDOUT) {
                err_printf("socks4_connect()->read(): timeout (proxy not responding)\n");
            } else {
                err_printf("socks4_connect()->read(): %s (proxy not responding)\n", strerror(errno));
            }
        } else {
            err_printf("socks4_connect()->read(): unexpected EOF (proxy closed connection)\n");
        }
        return -1;
    }

    if (buf[0] != 0 || buf[1] != 90) {
        err_printf("socks4_connect(): connection rejected (code: %d)\n", buf[1]);
        return -1;
    }

    return 0;
}

int socks5_connect(int sockfd, const char *dest_host, int dest_port, const char *username, const char *password)
{
    unsigned char buf[512];
    int len;
    ssize_t n;

    buf[0] = 5;
    if (username && password && *username && *password) {
        buf[1] = 2;
        buf[2] = 0;
        buf[3] = 2;
        len = 4;
    } else {
        buf[1] = 1;
        buf[2] = 0;
        len = 3;
    }

    if (safe_write_with_timeout(sockfd, buf, len, 10) != len) {
        if (errno == ETIMEDOUT) {
            err_printf("socks5_connect()->write(): timeout (proxy not responding during handshake)\n");
        } else {
            err_printf("socks5_connect()->write(): %s (proxy disconnected during handshake)\n", strerror(errno));
        }
        return -1;
    }

    n = safe_read_with_timeout(sockfd, buf, 2, 10);
    if (n != 2) {
        if (n < 0) {
            if (errno == ETIMEDOUT) {
                err_printf("socks5_connect()->read(): timeout (proxy not responding or bad SOCKS5 server)\n");
            } else {
                err_printf("socks5_connect()->read(): %s (proxy not responding or bad SOCKS5 server)\n", strerror(errno));
            }
        } else {
            err_printf("socks5_connect()->read(): unexpected EOF (proxy closed connection)\n");
        }
        return -1;
    }

    if (buf[0] != 5) {
        err_printf("socks5_connect(): invalid SOCKS version (got %d, expected 5)\n", buf[0]);
        return -1;
    }

    if (buf[1] == 2) {
        int ulen = strlen(username);
        int plen = strlen(password);
        buf[0] = 1;
        buf[1] = ulen;
        memcpy(buf + 2, username, ulen);
        buf[2 + ulen] = plen;
        memcpy(buf + 3 + ulen, password, plen);

        len = 3 + ulen + plen;
        if (safe_write_with_timeout(sockfd, buf, len, 10) != len) {
            if (errno == ETIMEDOUT) {
                err_printf("socks5_connect()->write(auth): timeout (proxy not responding during authentication)\n");
            } else {
                err_printf("socks5_connect()->write(auth): %s (proxy disconnected during authentication)\n", strerror(errno));
            }
            return -1;
        }

        n = safe_read_with_timeout(sockfd, buf, 2, 10);
        if (n != 2) {
            if (n < 0) {
                if (errno == ETIMEDOUT) {
                    err_printf("socks5_connect()->read(auth): timeout (proxy did not complete authentication)\n");
                } else {
                    err_printf("socks5_connect()->read(auth): %s (proxy did not complete authentication)\n", strerror(errno));
                }
            } else {
                err_printf("socks5_connect()->read(auth): unexpected EOF (proxy closed connection during authentication)\n");
            }
            return -1;
        }

        if (buf[1] != 0) {
            err_printf("socks5_connect(): authentication failed\n");
            return -1;
        }
    } else if (buf[1] != 0) {
        err_printf("socks5_connect(): no acceptable auth method\n");
        return -1;
    }

    buf[0] = 5;
    buf[1] = 1;
    buf[2] = 0;
    buf[3] = 3;
    len = strlen(dest_host);
    buf[4] = len;
    memcpy(buf + 5, dest_host, len);
    buf[5 + len] = (dest_port >> 8) & 0xFF;
    buf[6 + len] = dest_port & 0xFF;

    len = 7 + len;
    if (safe_write_with_timeout(sockfd, buf, len, 10) != len) {
        if (errno == ETIMEDOUT) {
            err_printf("socks5_connect()->write(connect): timeout (proxy not responding while establishing tunnel)\n");
        } else {
            err_printf("socks5_connect()->write(connect): %s (proxy disconnected while establishing tunnel)\n", strerror(errno));
        }
        return -1;
    }

    n = safe_read_with_timeout(sockfd, buf, 4, 10);
    if (n < 4) {
        if (n < 0) {
            if (errno == ETIMEDOUT) {
                err_printf("socks5_connect()->read(connect): timeout (proxy did not confirm tunnel)\n");
            } else {
                err_printf("socks5_connect()->read(connect): %s (proxy did not confirm tunnel)\n", strerror(errno));
            }
        } else {
            err_printf("socks5_connect()->read(connect): unexpected EOF (proxy closed connection before confirmation)\n");
        }
        return -1;
    }

    if (buf[1] != 0) {
        err_printf("socks5_connect(): connection failed (code: %d)\n", buf[1]);
        return -1;
    }

    int skip = 0;
    if (buf[3] == 1)
        skip = 4 + 2;
    else if (buf[3] == 3) {
        unsigned char alen;
        if (safe_read_with_timeout(sockfd, &alen, 1, 10) != 1)
            return -1;
        skip = alen + 2;
    } else if (buf[3] == 4)
        skip = 16 + 2;

    if (skip > 0) {
        unsigned char tmp[256];
        if (skip > (int)sizeof(tmp))
            skip = sizeof(tmp);
        if (safe_read_with_timeout(sockfd, tmp, skip, 10) != skip)
            return -1;
    }

    return 0;
}

int http_connect(int sockfd, const char *dest_host, int dest_port, const char *username, const char *password)
{
    char buf[2048];
    char auth[512];
    int len;

    len = snprintf(buf, sizeof(buf), "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n", 
        dest_host, dest_port, dest_host, dest_port);

    if (username && password && *username && *password) {
        char credentials[256];
        snprintf(credentials, sizeof(credentials), "%s:%s", username, password);

        char b64[512];
        int i, j = 0;
        static const char *base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        int clen = strlen(credentials);

        for (i = 0; i < clen; i += 3) {
            unsigned char a = credentials[i];
            unsigned char b = (i + 1 < clen) ? credentials[i + 1] : 0;
            unsigned char c = (i + 2 < clen) ? credentials[i + 2] : 0;

            b64[j++] = base64[a >> 2];
            b64[j++] = base64[((a & 3) << 4) | (b >> 4)];
            b64[j++] = (i + 1 < clen) ? base64[((b & 15) << 2) | (c >> 6)] : '=';
            b64[j++] = (i + 2 < clen) ? base64[c & 63] : '=';
        }
        b64[j] = '\0';

        len += snprintf(buf + len, sizeof(buf) - len, "Proxy-Authorization: Basic %s\r\n", b64);
    }

    len += snprintf(buf + len, sizeof(buf) - len, "\r\n");

    if (safe_write_with_timeout(sockfd, buf, len, 10) != len) {
        if (errno == ETIMEDOUT) {
            err_printf("http_connect()->write(): timeout (proxy not responding)\n");
        } else {
            err_printf("http_connect()->write(): %s (proxy disconnected)\n", strerror(errno));
        }
        return -1;
    }

    len = 0;
    while (len < (int)sizeof(buf) - 1) {
        int n = safe_read_with_timeout(sockfd, buf + len, 1, 10);
        if (n <= 0) {
            if (n < 0) {
                if (errno == ETIMEDOUT) {
                    err_printf("http_connect()->read(): timeout (proxy not responding)\n");
                } else {
                    err_printf("http_connect()->read(): %s (proxy not responding)\n", strerror(errno));
                }
            } else {
                err_printf("http_connect()->read(): unexpected EOF (proxy closed connection)\n");
            }
            return -1;
        }
        len += n;
        if (len >= 4 && strncmp(buf + len - 4, "\r\n\r\n", 4) == 0)
            break;
    }
    buf[len] = '\0';

    if (strncmp(buf, "HTTP/1.", 7) != 0) {
        err_printf("http_connect(): invalid HTTP response (not an HTTP proxy)\n");
        return -1;
    }

    char *status = buf + 9;
    int status_code = atoi(status);

    if (status_code != 200) {
        err_printf("http_connect(): connection failed (HTTP %d - proxy rejected request)\n", status_code);
        return -1;
    }

    return 0;
}

int connect_through_proxy(int sockfd, proxy *p, const char *dest_host, int dest_port)
{
    if (!p)
        return -1;

    struct sockaddr *proxy_addr;
    socklen_t proxy_addrlen;

    if (p->is_ipv6) {
        proxy_addr = (struct sockaddr *)&p->addr6;
        proxy_addrlen = sizeof(p->addr6);
    } else {
        proxy_addr = (struct sockaddr *)&p->addr;
        proxy_addrlen = sizeof(p->addr);
    }

    if (connect(sockfd, proxy_addr, proxy_addrlen) == -1) {
        if (errno != EINPROGRESS) {
            err_printf("connect_through_proxy()->connect(): %s (proxy may be offline)\n", strerror(errno));
            return -1;
        }
    }

    fd_set wfds;
    struct timeval tv;
    FD_ZERO(&wfds);
    FD_SET(sockfd, &wfds);
    tv.tv_sec = 30;
    tv.tv_usec = 0;

    int ret = select(sockfd + 1, NULL, &wfds, NULL, &tv);
    if (ret <= 0) {
        err_printf("connect_through_proxy()->select(): timeout or error (proxy may be offline or slow)\n");
        return -1;
    }

    int err;
    socklen_t errlen = sizeof(err);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0 || err != 0) {
        err_printf("connect_through_proxy()->getsockopt(): %s (proxy refused connection)\n", strerror(err ? err : errno));
        return -1;
    }

    const char *ptype_name = "UNKNOWN";
    switch (p->type) {
    case PROXY_SOCKS4:
        ptype_name = "SOCKS4";
        break;
    case PROXY_SOCKS5:
        ptype_name = "SOCKS5";
        break;
    case PROXY_HTTP:
        ptype_name = "HTTP";
        break;
    case PROXY_HTTPS:
        ptype_name = "HTTPS";
        break;
    default:
        err_printf("connect_through_proxy(): unsupported proxy type\n");
        return -1;
    }

    info_printf("Proxy %s:%d (%s) connected, negotiating tunnel to %s:%d\n",
                p->host, p->port, ptype_name, dest_host, dest_port);

    int result;
    switch (p->type) {
    case PROXY_SOCKS4:
        result = socks4_connect(sockfd, dest_host, dest_port, p->username);
        break;
    case PROXY_SOCKS5:
        result = socks5_connect(sockfd, dest_host, dest_port, p->username, p->password);
        break;
    case PROXY_HTTP:
    case PROXY_HTTPS:
        result = http_connect(sockfd, dest_host, dest_port, p->username, p->password);
        break;
    default:
        result = -1;
        break;
    }

    if (result == 0) {
        info_printf("Proxy %s:%d (%s) is online and tunneling %s:%d\n",
                    p->host, p->port, ptype_name, dest_host, dest_port);
    } else {
        info_printf("Proxy %s:%d (%s) is reachable but negotiation failed\n",
                    p->host, p->port, ptype_name);
    }

    return result;
}
