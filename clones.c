/* Copyright (c) 2002 Maciej Freudenheim <fahren@bochnia.pl>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/time.h>
#ifdef _USE_POLL
  #include <sys/poll.h>
#endif
#include <errno.h>
#include "defs.h"
#include "main.h"
#include "irc.h"
#include "clones.h"
#include "command.h"
#include "parse.h"

#include "hide.reasons"
enemy    *root, *tail, *h_clone[XHASH_CLONE];
int    xall, xsendint;

enemy *new_clone(int family)
{
    enemy *p, *h;

    if (root == 0)
        p = root = (enemy *)malloc(sizeof(enemy));
    else
        p = tail->next = (enemy *)malloc(sizeof(enemy));
    
    if (p == 0) {
        err_printf("new_clone()->malloc(): %s\n", strerror(errno));
        return 0;
    }
    
    memset(p, 0, sizeof(enemy));
    p->parent = tail;
    tail = p;

    
    char *nick = dict_nick();
    if (nick == NULL) {
        nick = random_nick(0);
    }
    p->nick = strdup(nick);
    if (p->nick == 0) {
        err_printf("new_clone()->strdup(): %s\n", strerror(errno));
        if (root == p)
            root = tail = 0;
        else
            p->parent->next = 0;
        free(p);
        return 0;
    }

    if ((p->fd = socket(family, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        err_printf("new_clone()->socket(): %s\n", strerror(errno));
        if (root == p)
            root = tail = 0;
        else
            p->parent->next = 0;
        free(p->nick); free(p);
        return 0;
    }

    if (fcntl(p->fd, F_SETFL, O_NONBLOCK) == -1) {
        err_printf("new_clone()->fcntl(): %s\n", strerror(errno));
        if (root == p)
            root = tail = 0;
        else
            p->parent->next = 0;
        close(p->fd); free(p->nick); free(p);
        return 0;
    }

    p->hash = hashU(p->nick, XHASH_CLONE);
    if ((h = h_clone[p->hash]))
        h->h_parent = p;
    p->h_next = h;
    h_clone[p->hash] = p;
    
    if (p->fd > hsock)
        hsock = p->fd;
    
#ifdef _USE_POLL
    p->pfd = &pfd_array[p->fd];
    p->pfd->fd = p->fd;
    p->pfd->events = 0;
#endif

#ifdef _ENEMY_DEBUG
    fprintf(fdebug, "+ Created clone %s (xcnt=%d, xall=%d, xcon=%d)\n",
            p->nick, xcnt, xall, xconnect.connecting);
#endif

    xall++;
    return p;
}

void kill_clone(enemy *c, int print)
{
    enemy *p;

    if (print) {
        if (xconnect.log_fd) {
            logit("<< Killing clone %s\n", c->nick);
        }
        cdel_printf("Killing clone %s.\n", c->nick);
    }
    
    if (c->connection_in_progress) {
        c->connection_in_progress = 0;
        if (xconnect.active_connects > 0) {
            xconnect.active_connects--;
        }
    }
    
#ifdef _ENEMY_DEBUG
    fprintf(fdebug, "+ Killing clone %s (xcnt=%d, xall=%d, xcon=%d)\n",
            c->nick, xcnt, xall, xconnect.connecting);
#endif
    
    if (hsock == c->fd) {
        hsock = 0;
        for (p = root; p; p = p->next)
            if (p->fd > hsock && p != c)
                hsock = p->fd;
    }
    
    if (--xall == xcnt && !xconnect.connecting && c->connected != 3) {
        if (xall) {
            if (xconnect.log_fd) {
                logit("<< All %d clones has been connected to %s.\n", xall, xconnect.ircserver);
            }
            cinfo_printf("All %d clones has been connected to %s.\n", xall, xconnect.ircserver);
        }
        if (xconnect.ident_file)
            set_ident(xconnect.ident_org, 0);
    }
    
    if (c->connected == 3) { // jest na IRC'u
        xclone *cl;
        xchan *ch;
        for (ch = chanroot; ch; ch = ch->next) // usuwamy z wszystkich kanalow
            if ((cl = find_clone(ch, c)))
                del_clone(ch, cl);
        xsend(c->fd, "QUIT :%s\n", "We are the enemy, you shall fear [PT]");
        free(c->read_buf); free(c->address);
        if (--xcnt == 0) {
            if (xconnect.log_fd) {
                logit("<< All clones disconnected from %s.\n", xconnect.ircserver);
            }
            cdel_printf("All clones disconnected from %s.\n", xconnect.ircserver);
        }
    }

    if (!xall && !xconnect.connecting && !xconnect.active_connects) {    
        if (xconnect.log_fd) {
            logit("<< All clones has been killed.\n");
        }
        cdel_printf("All clones has been killed.\n");
        free(xconnect.ircserver);
        xconnect.ircserver = 0;    // musimy od nowa w /load podaæ ircserver
        xconnect.ircport = DEF_IRCPORT;
    }
    
#ifdef _USE_POLL
    c->pfd->fd = -1;
    c->pfd->events = 0;
#endif
    
    close(c->fd);

    if (c == tail)
        tail = c->parent;
    else if (c->next)
        c->next->parent = c->parent;
    
    if (c == root)
        root = c->next;
    else
        c->parent->next = c->next;
    
    if (c == pingtail)
        pingtail = c->pingparent;
    else if (c->ping)
        c->ping->pingparent = c->pingparent;
    
    if (c == ping)
        ping = c->ping;
    else if (c->pingparent)
        c->pingparent->ping = c->ping;

    // tablica hashow
    if (c->h_next)
        c->h_next->h_parent = c->h_parent;
    if (h_clone[c->hash] == c)
        h_clone[c->hash] = c->h_next;
    else
        c->h_parent->h_next = c->h_next;
    
    free(c->nick);
    free(c);
}

enemy *is_clone(char *nick)
{
    enemy *k;
    
    for (k = h_clone[hashU(nick, XHASH_CLONE)]; k; k = k->h_next)
        if (!x_strcasecmp(k->nick, nick))
            return k;

    return 0;
}

void add_rejoin(enemy *c, char *chan)
{
    if (c->rejoin_buf == 0) {
        c->rejoin_buf = strdup(chan);
        c->rejoins = 1;
    } else {
        char *comma, *p;
        int i;
        for (comma = p = c->rejoin_buf; *p; p++)
            if (*p == ',') {
                if (!x_strncasecmp(comma, chan, p - comma))
                    return; // juz dodany
                comma = p + 1;
            }
        if (comma == c->rejoin_buf && !x_strcasecmp(comma, chan))
            return; // nie bylo przecinka, a kanaly sa takie same
        i = p - c->rejoin_buf; // i = strlen(c->rejoin_buf);
        p = (char *)realloc(c->rejoin_buf, i + strlen(chan) + 2); // 2 na ',' i '\0'
        if (p == 0) {
            err_printf("add_rejoin()->realloc(): %s\n", strerror(errno));
            return;
        }
        p[i] = ',';
        strcpy(&p[i+1], chan);
        c->rejoin_buf = p;
        c->rejoins++;
    }
}

int put_clone(enemy *x, char *buf, int len)
{
    char *str = strdup(buf), *w;
    int pp = 1 + len / 100;
    
    w = newsplit(&str);
    if (!x_strcasecmp(w, "MODE")) {
        char *ch = newsplit(&str);
        int i = 0;
        w = newsplit(&str);
        if (w == 0 || *w == '\n') // MODE #chan \n
            i += 1;
        else
            for (; *w; w++)
                if (*w == 'n' || *w == 't' || *w == 'i' || \
                    *w == 'm' || *w == 'p' || *w == 's')
                    i += 3;
                else if (*w != '+' && *w != '-' && *w != '\n')
                    i += 1;
        while (newsplit(&str))
            i += 2;
        while (splitnicks(&ch))
            pp += i;
    } else if (!x_strcasecmp(w, "KICK")) {
        char *ch = newsplit(&str);
        int i = 0;
        w = newsplit(&str);
        while (splitnicks(&w))
            i += 3;
        while (splitnicks(&ch))
            pp += i;
    } else if (!x_strcasecmp(w, "TOPIC")) {
        pp += 1;
        w = newsplit(&str); // chan
        if (*str) // if topic change
            while (splitnicks(&w)) // dla kazdego kanalu
                pp += 2;
    } else if (!x_strcasecmp(w, "PRIVMSG") || !x_strcasecmp(buf, "NOTICE")) {
        w = newsplit(&str); // target
        while (splitnicks(&w))
            pp += 1;
    } else if (!x_strcasecmp(w, "INVITE") || !x_strcasecmp(buf, "NICK")) {
        pp += 3;
    } else if (!x_strcasecmp(w, "JOIN")) {
        w = newsplit(&str);
        while (splitnicks(&w))
            pp += 2;
    } else if (!x_strcasecmp(w, "PART")) {
        w = newsplit(&str);
        while (splitnicks(&w))
            pp += 4;
    } else if (!x_strcasecmp(w, "AWAY")) {
        if (*str)
            pp += 2;
        else
            pp += 1;
    } else if (!x_strcasecmp(w, "WHO")) {
        w = newsplit(&str);
        while (splitnicks(&w))
            pp += 3;
    } else // inne smieci
        pp++;

    write(x->fd, buf, len);
    x->pp += pp;

    return pp;
}
