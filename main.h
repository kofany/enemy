#ifndef __MAIN_H
#define __MAIN_H

typedef struct string_t string;

struct string_t {
	char *string;
	unsigned char len, start, csum, diff;
};

#ifdef _ENEMY_DEBUG
	extern FILE *fdebug;
#endif

#define XBSIZE_READ 4096
#ifndef __FreeBSD__
// #define _USE_POLL 	1 a
#endif

#ifdef _USE_POLL
#define	POLL_ARRAYSIZE		1000
#define POLLSETREADFLAGS        POLLIN	
#define POLLSETWRITEFLAGS       POLLOUT
#define POLLERRORFLAGS          (POLLERR|POLLHUP|POLLNVAL)
#define POLLREADFLAGS		(POLLSETREADFLAGS|POLLERRORFLAGS)
#define POLLWRITEFLAGS		(POLLSETWRITEFLAGS|POLLERRORFLAGS)
#define SET_READ_EVENT(c)       c->events |= POLLSETREADFLAGS
#define SET_WRITE_EVENT(c)      c->events |= POLLSETWRITEFLAGS
#define CLR_READ_EVENT(c)       c->events &= ~POLLSETREADFLAGS
#define CLR_WRITE_EVENT(c)      c->events &= ~POLLSETWRITEFLAGS
#define IS_READ_EVENT(c)        c->revents & POLLREADFLAGS
#define IS_WRITE_EVENT(c)       c->revents & POLLWRITEFLAGS
#define IS_ERROR_EVENT(c)       c->revents & POLLERRORFLAGS 
#endif
	
extern char buf[], *read_str;
extern int debuglvl, hsock;
#ifdef _USE_POLL
extern struct pollfd pfd_array[];
#endif
extern string xinfo[], xrealnames[];
extern const float lxrealnames;

int xrand(float);
int x_tolower(int);
int x_strcmp(const char *, const char *);
int x_strncmp(const char *, const char *, int);
int x_strcasecmp(const char *, const char *);
int x_strncasecmp(const char *, const char *, int);

void set_signals(void);
void enemy_exit(int);

void create_strings(string *, char **, int);

void check_ident_file(void);
void set_ident(char *, int);

char *newsplit(char **);
char *xnewsplit(int, char **);
char *splitnicks(char **);

#endif
