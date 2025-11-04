/* Proxy support implementation
 * Supports HTTP, HTTPS, SOCKS4, SOCKS5 proxies
 * IPv4 and IPv6 support
 * Open and password-protected proxies
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
#include "defs.h"
#include "main.h"
#include "command.h"

proxy *parse_proxy_line(char *line, enum proxy_type default_type)
{
    proxy *p;
    char *scheme_end, *at_sign, *colon1, *colon2, *colon3, *bracket_start, *bracket_end;
    char buffer[MAX_PROXY_LINE];
    char *work;
    enum proxy_type ptype = default_type;
    int is_ipv6 = 0;

    if (!line || !*line)
        return NULL;

    strncpy(buffer, line, MAX_PROXY_LINE - 1);
    buffer[MAX_PROXY_LINE - 1] = '\0';

    for (char *s = buffer; *s; s++)
        if (*s == '\n' || *s == '\r')
            *s = '\0';

    work = buffer;
    while (isspace(*work)) work++;
    if (!*work || *work == '#')
        return NULL;

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
        work = scheme_end + 3;
    }
    p->type = ptype;

    at_sign = strchr(work, '@');
    if (at_sign) {
        *at_sign = '\0';
        char *user_pass = work;
        work = at_sign + 1;

        colon1 = strchr(user_pass, ':');
        if (colon1) {
            *colon1 = '\0';
            p->username = strdup(user_pass);
            p->password = strdup(colon1 + 1);
        } else {
            p->username = strdup(user_pass);
        }
    }

    bracket_start = strchr(work, '[');
    if (bracket_start) {
        is_ipv6 = 1;
        bracket_end = strchr(bracket_start, ']');
        if (!bracket_end) {
            del_proxy(p);
            return NULL;
        }
        *bracket_end = '\0';
        p->host = strdup(bracket_start + 1);
        work = bracket_end + 1;
        if (*work == ':') {
            p->port = atoi(work + 1);
        }
    } else {
        colon1 = strchr(work, ':');
        if (!colon1) {
            del_proxy(p);
            return NULL;
        }
        *colon1 = '\0';
        p->host = strdup(work);
        work = colon1 + 1;

        colon2 = strchr(work, ':');
        if (colon2) {
            *colon2 = '\0';
            p->port = atoi(work);
            work = colon2 + 1;

            colon3 = strchr(work, ':');
            if (colon3) {
                *colon3 = '\0';
                if (!p->username)
                    p->username = strdup(work);
                if (!p->password)
                    p->password = strdup(colon3 + 1);
            } else {
                if (!p->username && *work)
                    p->username = strdup(work);
            }
        } else {
            p->port = atoi(work);
        }
    }

    if (!p->host || p->port <= 0 || p->port > 65535) {
        del_proxy(p);
        return NULL;
    }

    p->is_ipv6 = is_ipv6;

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = is_ipv6 ? AF_INET6 : AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", p->port);

    if (getaddrinfo(p->host, port_str, &hints, &result) == 0) {
        if (result->ai_family == AF_INET6) {
            memcpy(&p->addr6, result->ai_addr, sizeof(struct sockaddr_in6));
            p->is_ipv6 = 1;
        } else {
            memcpy(&p->addr, result->ai_addr, sizeof(struct sockaddr_in));
        }
        freeaddrinfo(result);
    } else {
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

    if (write(sockfd, buf, len) != len) {
        err_printf("socks4_connect()->write(): %s\n", strerror(errno));
        return -1;
    }

    if (read(sockfd, buf, 8) != 8) {
        err_printf("socks4_connect()->read(): %s\n", strerror(errno));
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

    if (write(sockfd, buf, len) != len) {
        err_printf("socks5_connect()->write(): %s\n", strerror(errno));
        return -1;
    }

    if (read(sockfd, buf, 2) != 2) {
        err_printf("socks5_connect()->read(): %s\n", strerror(errno));
        return -1;
    }

    if (buf[0] != 5) {
        err_printf("socks5_connect(): invalid SOCKS version\n");
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
        if (write(sockfd, buf, len) != len) {
            err_printf("socks5_connect()->write(auth): %s\n", strerror(errno));
            return -1;
        }

        if (read(sockfd, buf, 2) != 2) {
            err_printf("socks5_connect()->read(auth): %s\n", strerror(errno));
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
    if (write(sockfd, buf, len) != len) {
        err_printf("socks5_connect()->write(connect): %s\n", strerror(errno));
        return -1;
    }

    if (read(sockfd, buf, 4) < 4) {
        err_printf("socks5_connect()->read(connect): %s\n", strerror(errno));
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
        if (read(sockfd, &alen, 1) != 1)
            return -1;
        skip = alen + 2;
    } else if (buf[3] == 4)
        skip = 16 + 2;

    if (skip > 0) {
        unsigned char tmp[256];
        read(sockfd, tmp, skip);
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

    if (write(sockfd, buf, len) != len) {
        err_printf("http_connect()->write(): %s\n", strerror(errno));
        return -1;
    }

    len = 0;
    while (len < sizeof(buf) - 1) {
        int n = read(sockfd, buf + len, 1);
        if (n <= 0) {
            err_printf("http_connect()->read(): %s\n", strerror(errno));
            return -1;
        }
        len += n;
        if (len >= 4 && strncmp(buf + len - 4, "\r\n\r\n", 4) == 0)
            break;
    }
    buf[len] = '\0';

    if (strncmp(buf, "HTTP/1.", 7) != 0) {
        err_printf("http_connect(): invalid HTTP response\n");
        return -1;
    }

    char *status = buf + 9;
    int status_code = atoi(status);

    if (status_code != 200) {
        err_printf("http_connect(): connection failed (HTTP %d)\n", status_code);
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
            err_printf("connect_through_proxy()->connect(): %s\n", strerror(errno));
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
        err_printf("connect_through_proxy()->select(): timeout or error\n");
        return -1;
    }

    int err;
    socklen_t errlen = sizeof(err);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0 || err != 0) {
        err_printf("connect_through_proxy()->getsockopt(): %s\n", strerror(err ? err : errno));
        return -1;
    }

    switch (p->type) {
    case PROXY_SOCKS4:
        return socks4_connect(sockfd, dest_host, dest_port, p->username);
    case PROXY_SOCKS5:
        return socks5_connect(sockfd, dest_host, dest_port, p->username, p->password);
    case PROXY_HTTP:
    case PROXY_HTTPS:
        return http_connect(sockfd, dest_host, dest_port, p->username, p->password);
    default:
        err_printf("connect_through_proxy(): unsupported proxy type\n");
        return -1;
    }
}
