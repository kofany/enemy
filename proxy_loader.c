/* Proxy loader implementation
 * Tests proxies for reachability and protocol detection
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include "defs.h"
#include "main.h"
#include "command.h"

typedef struct proxy_test_result {
    proxy *p;
    int reachable;
    enum proxy_type detected_type;
    int has_auth;
    long rtt_ms;
    char error_msg[256];
} proxy_test_result;

static int test_proxy_connect(proxy *p, int timeout_ms, long *rtt_ms)
{
    int sockfd;
    struct timeval start, end;
    fd_set wfds;
    struct timeval tv;
    struct sockaddr *addr;
    socklen_t addrlen;

    gettimeofday(&start, NULL);

    if (p->is_ipv6) {
        sockfd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        addr = (struct sockaddr *)&p->addr6;
        addrlen = sizeof(p->addr6);
    } else {
        sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        addr = (struct sockaddr *)&p->addr;
        addrlen = sizeof(p->addr);
    }

    if (sockfd == -1) {
        return -1;
    }

    if (fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1) {
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, addr, addrlen) == -1) {
        if (errno != EINPROGRESS) {
            close(sockfd);
            return -1;
        }
    }

    FD_ZERO(&wfds);
    FD_SET(sockfd, &wfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(sockfd + 1, NULL, &wfds, NULL, &tv);
    if (ret <= 0) {
        close(sockfd);
        if (ret == 0)
            errno = ETIMEDOUT;
        return -1;
    }

    int err;
    socklen_t errlen = sizeof(err);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0 || err != 0) {
        close(sockfd);
        if (err)
            errno = err;
        return -1;
    }

    gettimeofday(&end, NULL);
    if (rtt_ms) {
        *rtt_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
    }

    close(sockfd);
    return 0;
}

static int test_proxy_protocol(proxy *p, enum proxy_type test_type, const char *test_host, int test_port)
{
    int sockfd;
    struct sockaddr *addr;
    socklen_t addrlen;
    fd_set wfds;
    struct timeval tv;

    if (p->is_ipv6) {
        sockfd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        addr = (struct sockaddr *)&p->addr6;
        addrlen = sizeof(p->addr6);
    } else {
        sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        addr = (struct sockaddr *)&p->addr;
        addrlen = sizeof(p->addr);
    }

    if (sockfd == -1) {
        return -1;
    }

    if (fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1) {
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, addr, addrlen) == -1) {
        if (errno != EINPROGRESS) {
            close(sockfd);
            return -1;
        }
    }

    FD_ZERO(&wfds);
    FD_SET(sockfd, &wfds);
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    int ret = select(sockfd + 1, NULL, &wfds, NULL, &tv);
    if (ret <= 0) {
        close(sockfd);
        return -1;
    }

    int err;
    socklen_t errlen = sizeof(err);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0 || err != 0) {
        close(sockfd);
        return -1;
    }

    int result = -1;
    switch (test_type) {
    case PROXY_SOCKS5:
        result = socks5_connect(sockfd, test_host, test_port, p->username, p->password);
        break;
    case PROXY_SOCKS4:
        result = socks4_connect(sockfd, test_host, test_port, p->username);
        break;
    case PROXY_HTTP:
    case PROXY_HTTPS:
        result = http_connect(sockfd, test_host, test_port, p->username, p->password);
        break;
    default:
        break;
    }

    close(sockfd);
    return result;
}

static enum proxy_type detect_proxy_protocol(proxy *p, const char *test_host, int test_port)
{
    if (p->type != PROXY_NONE) {
        if (test_proxy_protocol(p, p->type, test_host, test_port) == 0)
            return p->type;
        return PROXY_NONE;
    }

    if (test_proxy_protocol(p, PROXY_SOCKS5, test_host, test_port) == 0)
        return PROXY_SOCKS5;

    if (test_proxy_protocol(p, PROXY_SOCKS4, test_host, test_port) == 0)
        return PROXY_SOCKS4;

    if (test_proxy_protocol(p, PROXY_HTTP, test_host, test_port) == 0)
        return PROXY_HTTP;

    return PROXY_NONE;
}

static void remove_proxy_from_list(proxy *p)
{
    if (!p)
        return;

    if (p->parent) {
        p->parent->next = p->next;
    } else {
        xconnect.proxy_list = p->next;
    }

    if (p->next) {
        p->next->parent = p->parent;
    } else {
        xconnect.proxy_tail = p->parent;
    }

    if (xconnect.current_proxy == p) {
        xconnect.current_proxy = p->next ? p->next : xconnect.proxy_list;
    }

    xconnect.proxy_count--;
    del_proxy(p);
}

int check_and_validate_proxies(const char *test_host, int test_port, int timeout_ms, int verbose)
{
    proxy *p, *next;
    int total = xconnect.proxy_count;
    int removed = 0;
    int working = 0;
    int socks5_count = 0;
    int socks4_count = 0;
    int http_count = 0;
    int current = 0;

    if (!xconnect.proxy_list) {
        err_printf("No proxies loaded.\n");
        return -1;
    }

    info_printf("Checking %d proxies from list...\n", total);

    for (p = xconnect.proxy_list; p; p = next) {
        next = p->next;
        current++;

        if (verbose) {
            info_printf("Checking %d/%d %s:%d", current, total, p->host, p->port);
            if (p->username && p->password) {
                printf(" (auth: %s)", p->username);
            }
            printf("\n");
        }

        long rtt_ms = 0;
        if (test_proxy_connect(p, timeout_ms, &rtt_ms) != 0) {
            if (verbose) {
                err_printf("Proxy removed (connect failed): %s:%d (%s)\n",
                          p->host, p->port, strerror(errno));
            }
            remove_proxy_from_list(p);
            removed++;
            continue;
        }

        enum proxy_type detected = detect_proxy_protocol(p, test_host, test_port);
        if (detected == PROXY_NONE) {
            if (verbose) {
                err_printf("Proxy removed (protocol negotiation failed): %s:%d\n",
                          p->host, p->port);
            }
            remove_proxy_from_list(p);
            removed++;
            continue;
        }

        p->type = detected;
        working++;

        const char *type_str = "UNKNOWN";
        switch (detected) {
        case PROXY_SOCKS5:
            type_str = "SOCKS5";
            socks5_count++;
            break;
        case PROXY_SOCKS4:
            type_str = "SOCKS4";
            socks4_count++;
            break;
        case PROXY_HTTP:
        case PROXY_HTTPS:
            type_str = "HTTP";
            http_count++;
            break;
        default:
            break;
        }

        if (verbose) {
            cinfo_printf("Proxy OK: %s:%d -> %s", p->host, p->port, type_str);
            if (p->username && p->password) {
                printf(" (auth)");
            }
            printf(" (rtt=%ldms)\n", rtt_ms);
        }
    }

    cinfo_printf("Summary: total=%d, removed=%d, working=%d", total, removed, working);
    if (working > 0) {
        printf(" (SOCKS5=%d, SOCKS4=%d, HTTP=%d)", socks5_count, socks4_count, http_count);
    }
    printf("\n");

    return working;
}

int save_validated_proxies(const char *filename)
{
    FILE *fp;
    proxy *p;
    int count = 0;

    fp = fopen(filename, "w");
    if (!fp) {
        err_printf("save_validated_proxies()->fopen(%s): %s\n", filename, strerror(errno));
        return -1;
    }

    for (p = xconnect.proxy_list; p; p = p->next) {
        const char *scheme = "";
        switch (p->type) {
        case PROXY_HTTP:
            scheme = "http://";
            break;
        case PROXY_HTTPS:
            scheme = "https://";
            break;
        case PROXY_SOCKS4:
            scheme = "socks4://";
            break;
        case PROXY_SOCKS5:
            scheme = "socks5://";
            break;
        default:
            scheme = "";
            break;
        }

        if (p->username && p->password) {
            fprintf(fp, "%s%s:%s@%s:%d\n", scheme, p->username, p->password, p->host, p->port);
        } else {
            fprintf(fp, "%s%s:%d\n", scheme, p->host, p->port);
        }
        count++;
    }

    fclose(fp);
    cinfo_printf("Saved %d validated proxies to %s\n", count, filename);
    return count;
}
