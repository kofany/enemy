#ifndef __CLONES_H
#define __CLONES_H

struct clone_t {
    int fd, connected, pp, cnt, hash, read_last, rejoins;
    char *nick, *address, *read_buf, *rejoin_buf;
#ifdef _USE_POLL
    struct pollfd *pfd;
#endif
    enemy *next, *parent, 
        *ping, *pingparent, 
        *h_next, *h_parent;
};

#define random_reason() xreasons[xrand(lxreasons)].string
#define random_ident() random_nick(0)
#define random_realname() xrealnames[xrand(lxrealnames)].string

// do jednego klonaa
#define xsend(d, c...)    { xsendint = snprintf(buf, XBSIZE, c); write(d, buf, xsendint); }

extern enemy *root, *tail;
extern enemy *h_clone[XHASH_CLONE];
extern char *hreasons[];
extern int xall, xsendint;

void add_rejoin(enemy *, char *);

enemy *new_clone(int family);
void kill_clone(enemy *, int);

enemy *is_clone(char *);

int put_clone(enemy *, char *, int);

#endif
