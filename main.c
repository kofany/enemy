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
// #include <termios.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#ifdef __linux__
  #include <sys/ptrace.h>
#endif
#ifdef _USE_POLL
  #include <sys/poll.h>
#else
  #include <sys/select.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include "defs.h"
#include "main.h"
#include "clones.h"
#include "irc.h"
#include "action.h"
#include "parse.h"
#include "command.h"
#include <stdio.h>

void printLogo() {
    printf("We are the\n"
           "  ___  ____  ___  ____ ___  __  __\n"
           " / _ \\/ __ \\/ _ \\/ __ `__ \\/ / / /\n"
           "/  __/ / / /  __/ / / / / / /_/ /\n"
           "\\___/_/ /_/\\___/_/ /_/ /_/\\__, /\n"
           "          you shall fear /____/\n");
}

int    hsock,
    read_last,
    debuglvl;
/* debug:
 * 1 = komunikaty o klonach jak ³adowanie, zsychnowanie kana³u
 * 2 = bardziej upierdliwe komunikaty jak killowanie, tworzenie #
 * 3 = chanmode'y
 * 4 = bardziej upierdliwe chanmode'y jak publiczne PRIVMSG'i
 * 5 = PING->PONG, connect()'y i to czego nie parsuje
 * 6 = wszystko
 */
#include "enemy.info"
char    buf[XBSIZE], read_buf[XBSIZE_READ], *read_ptr, *read_str;
#include "enemy.realnames"

#ifdef _USE_POLL
  struct pollfd pfd_array[POLL_ARRAYSIZE];
#endif

#ifdef _ENEMY_DEBUG
  FILE *fdebug;
#endif
int main(int argc, char **argv)
{
    int opt, opt_size = sizeof(opt);
#ifndef _USE_POLL
    fd_set    fd_read, fd_write, fd_exc;
#endif
    struct timeval tv;
    time_t current, last = 0;
    register enemy *p, *p_next;
    xchan *ch;
//    struct termios tio;

    if (argc > 1 && !x_strcasecmp(*(argv+1), "-d"))
        unlink(*argv);
    
    // g³upie czytanie z stdin'a
//    tcgetattr(0, &tio);
//    tio.c_lflag &= ~ICANON;
//    tcsetattr(0, TCSANOW, &tio);
    

#ifdef _ENEMY_DEBUG
    if ((fdebug = fopen("enemy.debug", "w")) == 0) {
        fprintf(stderr, "Error opening enemy.debug to write: %s\n",
                strerror(errno));
        exit(1);
    } else  {
        current = time(0);
        setlinebuf(fdebug);
        fprintf(fdebug, "- enemy: started debuging at %s", ctime(&current));
    }
#else
#endif // #else _ENEMY_DEBUG

    setvbuf(stdout, (char *)0, _IONBF, 0);
//    setlinebuf(stdout);
    srand((unsigned int) time(0) ^ getpid());
    create_strings(xinfo, hinfo, lxinfo);
    create_strings(xreasons, hreasons, lxreasons);
    create_strings(xrealnames, hrealnames, lxrealnames);
    set_signals(); // ma byc PO create_strings() - SIGSEGV

#ifdef _USE_POLL
    for (opt = 0; opt < POLL_ARRAYSIZE; opt++)
        pfd_array[opt].fd = -1;
    pfd_array[0].fd = 0;
    pfd_array[0].events |= POLLSETREADFLAGS;
#endif
    // defaults
    debuglvl = 2;
    xrejoindelay = 4;
    def_takemode = DEF_TAKEMODE;
    xconnect.ircserver = 0;
    xconnect.ircport = DEF_IRCPORT;
    xconnect.delay = 2;
    xconnect.max_concurrent_connections = 10;
    xconnect.max_retry_attempts = 3;
    xconnect.retry_backoff_sec = 5;
    xconnect.active_connects = 0;

    // info o programie, wygl±da gejowsko =)
//    printf("\n%s%s v%s%s%s (c) %s <%s%s%s>%s\n\n", COLOR_CYAN, xinfo[0].string, COLOR_LCYAN,
//            xinfo[1].string, COLOR_CYAN, xinfo[2].string, COLOR_LCYAN,
//            xinfo[3].string, COLOR_CYAN, COLOR_NORMAL);
    write(1, COLOR_GREEN, COLOR_LEN);
        printLogo();
//        printFileContent("logo");
        write(1, COLOR_CYAN, COLOR_LEN);
    printf("\n%s v", xinfo[0].string);
    write(1, COLOR_LCYAN, COLOR_LEN);
    write(1, xinfo[1].string, xinfo[1].len);
    write(1, COLOR_YELLOW, COLOR_LEN);
    printf(" %s \n", xinfo[2].string);
    write(1, COLOR_RED, COLOR_LEN);
    write(1, xinfo[3].string, xinfo[3].len);
    write(1, COLOR_YELLOW, COLOR_LEN);
    write(1, "\n\n", 3);
    write(1, COLOR_NORMAL, COLOR_NORLEN);
    
//    check_ident_file();
//    
//    switch ((opt = get_vhosts())) {
//        case -1:
//            err_printf("Exiting due to get_vhosts() fatal error.\n");
//            enemy_exit(1);
//            break;
//        case 0:
//            err_printf("Couldn't find any IP, using INADDR_ANY.\n\n");
//            break;
//        case 1:
//            cinfo_printf("Found %d vhost.\n\n", opt);
//            break;
//        default:
//            cinfo_printf("Found %d vhosts.\n\n", opt);
//            break;
//            
//    }
//        
    help_printf("Type 'help' for list of commands.\n");
    
    while (1) {
#ifdef _USE_POLL
        gettimeofday(&tv, 0);
        current = tv.tv_sec;
#else
        current = time(0);
#endif
        if (current > last) { // 1 sekunda
            if (xconnect.connecting > 0 && (!xconnect.timer || --xconnect.timer == 0)) {
                if (xconnect.bncserver)
                    do_connect_bnc();
                else
                    do_connect();
            }
        
            if (!xrejointimer || --xrejointimer == 0) {
                xrejointimer = xrejoindelay;
                if (current % 2) {
                    for (p = root; p; p = p->next) {
                        if (p->pp)
                            p->pp--;
                        if (p->rejoin_buf && p->pp < 10) {
                            xsend(p->fd, "JOIN %s\n", p->rejoin_buf)
                            free(p->rejoin_buf);
                            p->rejoin_buf = 0;
                            p->pp += (1 + (2 * p->rejoins));
                            p->rejoins = 0;
                        }
                    }
                } else {
                    for (p = root; p; p = p->next)
                        if (p->rejoin_buf && p->pp < 10) {
                            xsend(p->fd, "JOIN %s\n", p->rejoin_buf)
                            free(p->rejoin_buf);
                            p->rejoin_buf = 0;
                            p->pp += (1 + (2 * p->rejoins));
                            p->rejoins = 0;
                        }
                }
            } else if (current % 2)
                for (p = root; p; p = p->next)
                    if (p->pp)
                        p->pp--;
            
            if (current % 3) // 3 sekundy
                for (ch = chanroot; ch; ch = ch->next)
                    if (!(ch->inactive))
                        chan_action(ch);

            last = current;    
        }
#ifdef _USE_POLL
        if ((opt = poll(pfd_array, hsock+1, 1000 - (tv.tv_usec/1000))) == -1) {
            err_printf("poll(): %s\n", strerror(errno));
            break;
        } else if (opt == 0)
            continue;
#else
        FD_ZERO(&fd_read);
        FD_ZERO(&fd_write);
        FD_ZERO(&fd_exc);    
        
        FD_SET(0, &fd_read);
        for (p = root; p; p = p->next) {
            if (p->connected > 1)    // 2 lub 3 -- po polaczeniu sie
                FD_SET(p->fd, &fd_read);
            else         // 1 -- po do_connect()
                FD_SET(p->fd, &fd_write);
            FD_SET(p->fd, &fd_exc);
        }
        
        tv.tv_sec = 1; tv.tv_usec = 0;
        if ((opt = select(hsock+1, &fd_read, &fd_write, &fd_exc, &tv)) < 0) {
            err_printf("select(): %s\n", strerror(errno));
            break;
        } else if (opt == 0)
            continue;
#endif
        for (p = root; p; p = p_next) {
            p_next = p->next;
#ifdef _USE_POLL
            if (IS_ERROR_EVENT(p->pfd))
                kill_clone(p, 1);
            else if (IS_READ_EVENT(p->pfd)) {
#else
            if (FD_ISSET(p->fd, &fd_exc))
                kill_clone(p, 1);
            else if (FD_ISSET(p->fd, &fd_read)) {
#endif
                parse_act = 0;
                parse_op = 0;
                if (p->read_buf) {
                    read_last = p->read_last;
                    memmove((char *)read_buf, p->read_buf, read_last+1); // +1 na '\0'
                    free(p->read_buf);
                    p->read_buf = 0;
#ifdef _ENEMY_DEBUG2
                    fprintf(fdebug, "[%9s][%d] Trying to read() again, starting from(%d): '%s'\n",
                            p->nick, p->fd, read_last, read_buf);
#endif
                }
                do {
                    if ((opt = read(p->fd, read_buf+read_last, XBSIZE_READ-read_last-1)) <= 0) {
#ifdef _ENEMY_DEBUG2
                        fprintf(fdebug, "[%9s][%d] read() error (read_last=%d): %s\n",
                                p->nick, p->fd, read_last,
                                opt==-1? strerror(errno) : "eof");
#endif

                        if (opt == 0) {
                            if (p->retry_count < xconnect.max_retry_attempts) {
                                p->retry_count++;
                                xconnect.connecting++;
                                xconnect.timer = xconnect.retry_backoff_sec;
                                info_printf("Connection closed by remote for %s - queued retry %d/%d\n",
                                           p->nick, p->retry_count, xconnect.max_retry_attempts);
                            } else {
                                info_printf("Connection closed by remote for %s (max retries exceeded)\n", p->nick);
                            }
                            kill_clone(p, 1);
                        } else if (errno == EAGAIN && read_last) {
                            p->read_buf = strdup(read_buf);
                            p->read_last = read_last;
                        } else {
                            if (errno == ECONNRESET || errno == EPIPE || errno == ETIMEDOUT) {
                                if (p->retry_count < xconnect.max_retry_attempts) {
                                    p->retry_count++;
                                    xconnect.connecting++;
                                    xconnect.timer = xconnect.retry_backoff_sec;
                                    info_printf("Connection lost for %s: %s - queued retry %d/%d\n",
                                               p->nick, strerror(errno), p->retry_count, xconnect.max_retry_attempts);
                                } else {
                                    err_printf("read_sock() for %s: %s (max retries exceeded)\n", p->nick, strerror(errno));
                                }
                            } else {
                                err_printf("read_sock() for %s: %s\n", p->nick, strerror(errno));
                            }
                            kill_clone(p, 1);
                        }
                            
                        read_last = 0;
                        break;
                    }
                        
                    read_buf[opt+read_last] = 0;
#ifdef _ENEMY_DEBUG2
                    fprintf(fdebug, "[%9s][%d] read_buf(%d): '%s'\n", p->nick, p->fd, opt, read_buf);                
#endif
                    read_last = 0;
                    for (read_ptr = read_str = read_buf; *read_ptr; read_ptr++)
                        if (*read_ptr == '\n') {
                        //    if (*(read_ptr - 1) == '\r') // na NOTICE'y z bnc, ale po co?
                            *(read_ptr - 1) = 0; // ucinamy '\r'
                            parse_clone(p);
                            read_str = read_ptr + 1;
                        }
                    if (read_str != read_ptr) {
                        read_last = read_ptr - read_str;
                        memmove((char *)read_buf, read_str, read_last);
                        read_buf[read_last] = 0; // bez debuga do wyjebania
#ifdef _ENEMY_DEBUG2
                        fprintf(fdebug, "[%9s] no NL: read_buf(%d) = '%s'\n",
                                p->nick, read_last, read_buf);
#endif
                    }
                } while (read_last);
                while (parse_op)
                    clone_action(parse_mode_chan, parse_mode[--parse_op]);
                if (parse_act) chan_action(parse_act);
#ifdef _USE_POLL
            } else if (IS_WRITE_EVENT(p->pfd)) { // 1, po do_connect()
#else
            } else if (FD_ISSET(p->fd, &fd_write)) { // 1, po do_connect()
#endif
                getsockopt(p->fd, SOL_SOCKET, SO_ERROR, &opt, &opt_size);
                if (opt) {
                    err_printf("connect(): %s\n", strerror(opt));
                    kill_clone(p, 1);
                    if (xconnect.connecting > 0) {
                        xconnect.timer = 1;
                    }
                } else {    // connect() przeszed³ bez b³êdów
#ifdef _ENEMY_DEBUG
                    fprintf(fdebug, "+ Clone %s connect()'ed, sending NICK + USER (xcnt=%d, xall=%d, xcon=%d)\n", p->nick, xcnt, xall, xconnect.connecting);
#endif
                    xdebug(5, "Clone %s connect()'ed, sending NICK + USER\n", p->nick);
                    p->connected = 2;
                    p->connection_in_progress = 0;
#ifdef _USE_POLL
                    CLR_WRITE_EVENT(p->pfd);
                    SET_READ_EVENT(p->pfd);
#endif
                    xsend(p->fd, "NICK %s\n", p->nick);
                    if (xconnect.bncserver) {
                        xsend(p->fd, "PASS %s\n", xconnect.bncpass);
                    }
                    //xsend(p->fd, "USER %s 0 * :%s\n",
                    //        random_ident(), random_realname());
                    xsend(p->fd, "USER %s 0 * :%s\n",
                            random_ident(), "fikumiku");
                    if (xconnect.bncserver) {
                        if (xconnect.vhost) {
                            xsend(p->fd, "VIP %s\n", next_bnc());
                        }
                        xsend(p->fd, "CONN %s %d\n", xconnect.ircserver, xconnect.ircport);
                    }
                }
            }
        }
        
        if (xpingreplies == -1) {
            cping = 0.0;
            root = ping;
            tail = pingtail;
            ping = pingtail = 0;
            
            for (p = root; p; p = p->ping) {
                p->parent = p->pingparent;
                p->next = p->ping;
            }

            xpingreplies = 0;
        }
        
#ifdef _USE_POLL    
        if (pfd_array[0].revents & POLLREADFLAGS)
#else
        if (FD_ISSET(0, &fd_read))
#endif
            parse_input();
    }
    
    enemy_exit(0);    
    return 0;
}
    
void enemy_exit(int status)
{
    enemy *p, *p_t;
    
    if (xall) {
        cdel_printf("Disconnecting all clones..\n");
        for (p = root; p; p = p_t) {
            p_t = p->next;
            kill_clone(p, 0);
        }
    }
    
    if (xconnect.log_fd) {
        time_t current = time(0);
        cdel_printf("Closing logfile '%s'.\n", xconnect.log_file);
        logit("-- Program exit(%d) %s", status, ctime(&current));
        close(xconnect.log_fd);
        free(xconnect.log_file);
    }
    
    // czy¶cimy xconnect -- po co? nie wiem :)
    free(xconnect.ircserver);
    del_vhost_all();
    if (xconnect.ident_file) {
        set_ident(xconnect.ident_org, 0);
        free(xconnect.ident_org);
        free(xconnect.ident_file);
    }
    
#ifdef _ENEMY_DEBUG
    fprintf(fdebug, "- enemy: fclose()\n");
    fclose(fdebug);
#endif
    
    setlinebuf(stdout);
    info_printf("Exiting..\n");
    exit(status);
}

void set_signals(void)
{
    signal(SIGINT, enemy_exit);
    signal(SIGQUIT, enemy_exit);
    signal(SIGILL, enemy_exit);
    signal(SIGTRAP, enemy_exit);
    signal(SIGABRT, enemy_exit);
    signal(SIGIOT, enemy_exit);
    signal(SIGFPE, enemy_exit);
    signal(SIGKILL, enemy_exit);
    signal(SIGUSR1, enemy_exit);
    signal(SIGSEGV, enemy_exit);
    signal(SIGUSR2, enemy_exit);
    signal(SIGPIPE, enemy_exit);
    signal(SIGTERM, enemy_exit);
    signal(SIGCHLD, enemy_exit);
//    signal(SIGCONT, enemy_exit);
//    signal(SIGSTOP, enemy_exit);
//    signal(SIGTSTP, enemy_exit);
    signal(SIGTTIN, enemy_exit);
    signal(SIGTTOU, enemy_exit);
}

void set_ident(char *ident, int oidentd2)
{
    int fd;
    if ((fd = open(xconnect.ident_file, O_WRONLY|O_TRUNC)) == -1) {
        err_printf("set_ident()->open(%s): %s\n", xconnect.ident_file, strerror(errno));
        free(xconnect.ident_file); free(xconnect.ident_org);
        xconnect.ident_file = 0;
    } else {
        if (ident) {
            if (oidentd2 == 1) {
                write(fd, "global {\n\treply = \"", 19);
                write(fd, ident, strlen(ident));
                write(fd, "\"\n}", 3);
            } else
                write(fd, ident, strlen(ident));
            write(fd, "\n", 1);
        } // jesli nie ma 'ident' to po prostu czysci plik (O_TRUNC)
        close(fd);
    }
}

void check_ident_file(void)
{
    char *fpath;
    struct stat sbuf;
    int fd;

    fpath = (char *)malloc(strlen(getenv("HOME")) + 16); // 16 na /.oidentd.conf + 0

    sprintf(fpath, "%s/.ispoof", getenv("HOME"));
    
    if (!stat(fpath, &sbuf)) {
        cinfo_printf("~/.ispoof found, using random idents.\n");
    } else {
        sprintf(fpath, "%s/.fakeid", getenv("HOME"));
        if (!stat(fpath, &sbuf)) {
            cinfo_printf("~/.fakeid found, using random idents.\n");
        } else {
            sprintf(fpath, "%s/.oidentd.conf", getenv("HOME"));
            if (!stat(fpath, &sbuf)) {
                cinfo_printf("~/.oidentd.conf found, using random idents.\n");
                xconnect.ident_oidentd2 = 1;
            } else {
                info_printf("~/.ispoof, ~/.fakeid or ~/.oidentd.conf not found\n    -- using default ident.\n");
                free(fpath);
                return;
            }
        }
    }

    if ((fd = open(fpath, O_RDWR)) == -1) {
        err_printf("check_ident_file()->open(%s): %s\n", fpath, strerror(errno));
        free(fpath);
        return;
    }
    
    xconnect.ident_file = fpath;
    
    if (sbuf.st_size > 0) {
        xconnect.ident_org = (char *) malloc(sbuf.st_size);
        if (xconnect.ident_org == 0) {
            err_printf("check_ident_file()->malloc(): %s\n", strerror(errno));
            free(xconnect.ident_file);
            xconnect.ident_file = 0;
            return;
        }
        read(fd, xconnect.ident_org, sbuf.st_size - 1);
        xconnect.ident_org[sbuf.st_size-1] = '\0';
    }
    close(fd);
}

void create_strings(string *s, char **h, int l)
{

    int i, j;
    unsigned char *p, sum = 0, xor;

    for (i = 0; i < l; i++, sum = 0) {
        p = s[i].string = (char *)malloc(s[i].len + 1);
        if (p == 0) {
            err_printf("main()->malloc(): %s\n", strerror(errno));
            enemy_exit(1);
        }
        memcpy(s[i].string, h[i], s[i].len + 1);
        xor = s[i].start;
        for (j = 0; j < s[i].len ; j++, p++) {
            *p ^= (xor += s[i].diff);
            sum ^= *p;
        }
        if (sum != s[i].csum) {
            p = 0;
            *p = 0;
        }
    }
}

int x_tolower(register int c)
{
    return ((c >= 'A' && c <= 'Z')? c + ('a' - 'A') : c);
}

int x_strcmp(register const char *a, register const char *b)
{
    while (*a == *b++)
        if (*a++ == 0)
            return 0;
    return -1;
}

int x_strncmp(register const char *a, register const char *b, register int n)
{
    do {
        if (*a != *b++)
            return -1;
        if (*a++ == 0)
            break;
    } while (--n);
    return 0;
}

int x_strcasecmp(register const char *a, register const char *b)
{
    while (x_tolower(*a) == x_tolower(*b++))
        if (*a++ == 0)
            return 0;
    
    return -1;
}

int x_strncasecmp(register const char *a, register const char *b, register int n)
{
    do {
        if (x_tolower(*a) != x_tolower(*b++))
            return -1;
        if (*a++ == 0)
            break;
    } while (--n);
    return 0;
}

int xrand(float c)
{
    return ((int) (c*rand()/(RAND_MAX+1.0)));
}

/*
 * newsplit() ukradzione z eggdropa;
 *
 * Copyright (C) 1997 Robey Pointer
 * Copyright (C) 1999, 2000, 2001, 2002 Eggheads Development Team
 */

char *newsplit(char **rest)
{
    register char *o, *r;
    o = *rest;
    if (*o == 0) return 0;
    while (*o == ' ')
        o++;
    if (!*o)
        return 0;
    r = o;
    while (*o && (*o != ' '))
        o++;
    if (*o)
        *o++ = 0;
    *rest = o;
    return r;
}

/* modyfikacja pozwalaj±ca wyci±æ interesuj±ce nas s³owo */

char *xnewsplit(int word, char **rest)
{
    register char *o, *r;
    o = *rest;
    while (*o == ' ')
        o++;
    if (!*o)
        return 0;
    r = o;
    while (*o) {
        if (*o == ' ') {
            if (--word) {
                while (*o == ' ')
                    o++;
                r = o;
            } else {
                *o++ = 0;
                break;
            }
        }
        o++;
    }
    *rest = o;
    return ((word > 1)? 0 : r);
}

char *splitnicks(char **rest)
{
    register char *o, *r;
    
    o = *rest;
    while (*o == ' ' || *o == ',')
        o++;
    if (!*o)
        return 0;
    r = o;
    while (*o && *o != ',')
        o++;
    if (*o)
        *o++ = 0;
    *rest = o;
        return r;
}

