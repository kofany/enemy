/* Proxy loader implementation
 * Concurrent validation and protocol detection for loaded proxies
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "defs.h"
#include "main.h"
#include "command.h"

#define PROXY_VALIDATOR_DEFAULT_TIMEOUT_MS 5000
#define PROXY_VALIDATOR_DEFAULT_CONCURRENCY 10
#define PROXY_VALIDATOR_MAX_CONCURRENCY 128

static const char *proxy_type_name(enum proxy_type type)
{
    switch (type) {
    case PROXY_HTTP:
        return "HTTP";
    case PROXY_HTTPS:
        return "HTTPS";
    case PROXY_SOCKS4:
        return "SOCKS4";
    case PROXY_SOCKS5:
        return "SOCKS5";
    default:
        return "UNKNOWN";
    }
}

static void remove_proxy_from_list(proxy *p)
{
    if (!p)
        return;

    if (p->parent)
        p->parent->next = p->next;
    else
        xconnect.proxy_list = p->next;

    if (p->next)
        p->next->parent = p->parent;
    else
        xconnect.proxy_tail = p->parent;

    if (xconnect.current_proxy == p)
        xconnect.current_proxy = p->next ? p->next : xconnect.proxy_list;

    xconnect.proxy_count--;
    del_proxy(p);
}

static long long proxy_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

static int open_proxy_socket(proxy *p, int timeout_ms, long *rtt_ms, char *errbuf, size_t errlen)
{
    int sockfd;
    struct sockaddr *addr;
    socklen_t addrlen;
    long long start_ms = proxy_now_ms();

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
        snprintf(errbuf, errlen, "socket(): %s", strerror(errno));
        return -1;
    }

    if (fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1) {
        snprintf(errbuf, errlen, "fcntl(O_NONBLOCK): %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, addr, addrlen) == -1) {
        if (errno != EINPROGRESS) {
            snprintf(errbuf, errlen, "connect(): %s", strerror(errno));
            close(sockfd);
            return -1;
        }
    } else {
        if (rtt_ms)
            *rtt_ms = (long)(proxy_now_ms() - start_ms);
        return sockfd;
    }

    struct pollfd pfd;
    pfd.fd = sockfd;
    pfd.events = POLLOUT;

    while (1) {
        long long now_ms = proxy_now_ms();
        int remaining = timeout_ms - (int)(now_ms - start_ms);
        if (remaining <= 0) {
            snprintf(errbuf, errlen, "connect timeout");
            close(sockfd);
            errno = ETIMEDOUT;
            return -1;
        }

        int ret = poll(&pfd, 1, remaining);
        if (ret == 0) {
            snprintf(errbuf, errlen, "connect timeout");
            close(sockfd);
            errno = ETIMEDOUT;
            return -1;
        }
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            snprintf(errbuf, errlen, "poll(): %s", strerror(errno));
            close(sockfd);
            return -1;
        }

        if (pfd.revents & (POLLOUT | POLLERR | POLLHUP)) {
            int err = 0;
            socklen_t errlen = sizeof(err);
            if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0) {
                snprintf(errbuf, errlen, "getsockopt(): %s", strerror(errno));
                close(sockfd);
                return -1;
            }
            if (err != 0) {
                snprintf(errbuf, errlen, "connect(): %s", strerror(err));
                close(sockfd);
                errno = err;
                return -1;
            }
            if (rtt_ms)
                *rtt_ms = (long)(proxy_now_ms() - start_ms);
            return sockfd;
        }
    }
}

static int perform_proxy_handshake(int sockfd, proxy *p, enum proxy_type type,
                                   const char *test_host, int test_port)
{
    switch (type) {
    case PROXY_SOCKS5:
        return socks5_connect(sockfd, test_host, test_port, p->username, p->password);
    case PROXY_SOCKS4:
        return socks4_connect(sockfd, test_host, test_port, p->username);
    case PROXY_HTTP:
    case PROXY_HTTPS:
        return http_connect(sockfd, test_host, test_port, p->username, p->password);
    default:
        errno = EINVAL;
        return -1;
    }
}

typedef struct proxy_validator_ctx {
    proxy **items;
    int total;
    int next_index;
    const char *test_host;
    int test_port;
    int timeout_ms;
    int verbose;
    int socks5_count;
    int socks4_count;
    int http_count;
    int working;
    int removed;
    pthread_mutex_t index_lock;
    pthread_mutex_t stats_lock;
    pthread_mutex_t log_lock;
} proxy_validator_ctx;

static void log_proxy_attempt(proxy_validator_ctx *ctx, const char *host, int port,
                              const char *message, const char *auth, int idx, int total)
{
    pthread_mutex_lock(&ctx->log_lock);
    info_printf("Checking %d/%d %s:%d", idx + 1, total, host, port);
    if (auth)
        printf(" %s", auth);
    if (message)
        printf(" %s", message);
    printf("\n");
    pthread_mutex_unlock(&ctx->log_lock);
}

static void log_proxy_success(proxy_validator_ctx *ctx, proxy *p, enum proxy_type type,
                              long rtt_ms, long long elapsed_ms)
{
    const char *type_name = proxy_type_name(type);
    pthread_mutex_lock(&ctx->log_lock);
    cinfo_printf("Proxy OK: %s:%d -> %s", p->host, p->port, type_name);
    if (p->username && p->password && *p->username && *p->password)
        printf(" (auth)");
    printf(" (connect=%ldms total=%lldms)\n", rtt_ms, elapsed_ms);
    pthread_mutex_unlock(&ctx->log_lock);
}

static void log_proxy_failure(proxy_validator_ctx *ctx, proxy *p, const char *reason,
                              long long elapsed_ms)
{
    pthread_mutex_lock(&ctx->log_lock);
    err_printf("Proxy removed: %s:%d (%s, total=%lldms)\n",
               p->host, p->port,
               (reason && *reason) ? reason : "validation failed",
               elapsed_ms);
    pthread_mutex_unlock(&ctx->log_lock);
}

static void *proxy_validator_worker(void *arg)
{
    proxy_validator_ctx *ctx = (proxy_validator_ctx *)arg;

    while (1) {
        int idx;
        proxy *p;

        pthread_mutex_lock(&ctx->index_lock);
        idx = ctx->next_index++;
        pthread_mutex_unlock(&ctx->index_lock);

        if (idx >= ctx->total)
            break;

        p = ctx->items[idx];
        long long start_ms = proxy_now_ms();
        char errbuf[256] = "";
        long connect_rtt_ms = 0;
        enum proxy_type detected = PROXY_NONE;
        int keep_proxy = 0;
        int connect_failed = 0;

        if (ctx->verbose) {
            const char *auth = (p->username && p->password && *p->username && *p->password)
                                   ? "(auth)"
                                   : NULL;
            log_proxy_attempt(ctx, p->host, p->port, NULL, auth, idx, ctx->total);
        }

        enum proxy_type order[4];
        int order_len = 0;
        if (p->type != PROXY_NONE) {
            order[order_len++] = p->type;
        } else {
            order[order_len++] = PROXY_SOCKS5;
            order[order_len++] = PROXY_SOCKS4;
            order[order_len++] = PROXY_HTTP;
        }
        order[order_len] = PROXY_NONE;

        for (int i = 0; i < order_len; i++) {
            enum proxy_type attempt_type = order[i];
            int sockfd = open_proxy_socket(p, ctx->timeout_ms, &connect_rtt_ms,
                                           errbuf, sizeof(errbuf));
            if (sockfd < 0) {
                connect_failed = 1;
                break;
            }

            if (ctx->verbose) {
                pthread_mutex_lock(&ctx->log_lock);
                info_printf("  -> Trying %s handshake\n", proxy_type_name(attempt_type));
                pthread_mutex_unlock(&ctx->log_lock);
            }

            if (perform_proxy_handshake(sockfd, p, attempt_type,
                                        ctx->test_host, ctx->test_port) == 0) {
                detected = attempt_type;
                keep_proxy = 1;
                close(sockfd);
                break;
            }

            close(sockfd);
            snprintf(errbuf, sizeof(errbuf), "%s negotiation failed",
                     proxy_type_name(attempt_type));
        }

        long long elapsed_ms = proxy_now_ms() - start_ms;

        if (keep_proxy && detected != PROXY_NONE) {
            p->validated = 1;
            p->is_active = 1;
            p->detected_type = detected;
            p->type = detected;
            p->last_rtt_ms = (int)connect_rtt_ms;
            p->has_auth = (p->username && p->password && *p->username && *p->password) ? 1 : 0;

            pthread_mutex_lock(&ctx->stats_lock);
            ctx->working++;
            switch (detected) {
            case PROXY_SOCKS5:
                ctx->socks5_count++;
                break;
            case PROXY_SOCKS4:
                ctx->socks4_count++;
                break;
            case PROXY_HTTP:
            case PROXY_HTTPS:
                ctx->http_count++;
                break;
            default:
                break;
            }
            pthread_mutex_unlock(&ctx->stats_lock);

            log_proxy_success(ctx, p, detected, connect_rtt_ms, elapsed_ms);
        } else {
            p->validated = 0;
            p->is_active = 0;
            p->detected_type = PROXY_NONE;
            p->last_rtt_ms = 0;
            p->has_auth = 0;

            pthread_mutex_lock(&ctx->stats_lock);
            ctx->removed++;
            pthread_mutex_unlock(&ctx->stats_lock);

            if (connect_failed && (!errbuf[0]))
                snprintf(errbuf, sizeof(errbuf), "connect failed");
            log_proxy_failure(ctx, p, errbuf, elapsed_ms);
        }
    }

    return NULL;
}

int check_and_validate_proxies(const char *test_host, int test_port, int timeout_ms, int verbose)
{
    proxy *p, *next;
    proxy_validator_ctx ctx;
    pthread_t threads[PROXY_VALIDATOR_MAX_CONCURRENCY];
    int concurrency;
    int thread_count;

    if (!xconnect.proxy_list) {
        err_printf("No proxies loaded.\n");
        return -1;
    }

    if (timeout_ms <= 0)
        timeout_ms = PROXY_VALIDATOR_DEFAULT_TIMEOUT_MS;

    if (xconnect.proxy_loader_concurrency > 0)
        concurrency = xconnect.proxy_loader_concurrency;
    else
        concurrency = PROXY_VALIDATOR_DEFAULT_CONCURRENCY;

    if (concurrency < 1)
        concurrency = 1;
    if (concurrency > PROXY_VALIDATOR_MAX_CONCURRENCY)
        concurrency = PROXY_VALIDATOR_MAX_CONCURRENCY;

    int total = xconnect.proxy_count;
    if (total <= 0) {
        err_printf("Proxy list is empty.\n");
        return -1;
    }

    ctx.items = (proxy **)calloc((size_t)total, sizeof(proxy *));
    if (!ctx.items) {
        err_printf("check_and_validate_proxies()->calloc(): %s\n", strerror(errno));
        return -1;
    }

    int idx = 0;
    for (p = xconnect.proxy_list; p; p = p->next) {
        ctx.items[idx++] = p;
        p->validated = 0;
        p->is_active = 0;
        p->detected_type = PROXY_NONE;
        p->last_rtt_ms = 0;
        p->has_auth = 0;
    }

    ctx.total = total;
    ctx.next_index = 0;
    ctx.test_host = test_host;
    ctx.test_port = test_port;
    ctx.timeout_ms = timeout_ms;
    ctx.verbose = verbose;
    ctx.socks5_count = 0;
    ctx.socks4_count = 0;
    ctx.http_count = 0;
    ctx.working = 0;
    ctx.removed = 0;
    pthread_mutex_init(&ctx.index_lock, NULL);
    pthread_mutex_init(&ctx.stats_lock, NULL);
    pthread_mutex_init(&ctx.log_lock, NULL);

    thread_count = concurrency;
    if (thread_count > total)
        thread_count = total;

    info_printf("Validating %d proxies (concurrency=%d, timeout=%dms)\n",
                total, thread_count, timeout_ms);

    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&threads[i], NULL, proxy_validator_worker, &ctx) != 0) {
            err_printf("pthread_create(): %s\n", strerror(errno));
            thread_count = i;
            break;
        }
    }

    for (int i = 0; i < thread_count; i++)
        pthread_join(threads[i], NULL);

    for (p = xconnect.proxy_list; p; p = next) {
        next = p->next;
        if (!p->validated)
            remove_proxy_from_list(p);
    }

    pthread_mutex_destroy(&ctx.index_lock);
    pthread_mutex_destroy(&ctx.stats_lock);
    pthread_mutex_destroy(&ctx.log_lock);
    free(ctx.items);

    cinfo_printf("Summary: total=%d, removed=%d, working=%d",
                total, ctx.removed, ctx.working);
    if (ctx.working > 0) {
        printf(" (SOCKS5=%d, SOCKS4=%d, HTTP=%d)",
               ctx.socks5_count, ctx.socks4_count, ctx.http_count);
    }
    printf("\n");

    return ctx.working;
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

        if (p->username && p->password && *p->username && *p->password)
            fprintf(fp, "%s%s:%s@%s:%d\n", scheme, p->username, p->password, p->host, p->port);
        else
            fprintf(fp, "%s%s:%d\n", scheme, p->host, p->port);
        count++;
    }

    fclose(fp);
    cinfo_printf("Saved %d validated proxies to %s\n", count, filename);
    return count;
}
