#ifndef __COMMAND_H
#define __COMMAND_H
// d
#define MAX_CMDLEN	2048
#define MAX_NICKLEN	9
#define DEF_IRCPORT 	6667
#define DEF_TAKEMODE	1 // 0 = deop, 1 = kick, 2 = close(kick)

#define logit(c...) { 	gettimeofday(&tv_log, 0); 			\
			tm_log = localtime((time_t *)&tv_log.tv_sec);		\
			xsendint = strftime(buf, XBSIZE, "%X", tm_log);	\
			xsendint += snprintf(buf+xsendint, XBSIZE-xsendint, ".%.6ld ", tv_log.tv_usec);	\
			xsendint += snprintf(buf+xsendint, XBSIZE-xsendint, c); 			\
			write(xconnect.log_fd, buf, xsendint); }

typedef struct vhosts_t vhost;
typedef struct xaddress_t xaddress;

struct vhosts_t {
	char *name;
	struct sockaddr_in *addr;
	struct sockaddr_in6 *addr6;
	vhost *next, *parent;
};


struct server_info {
    char *ircserver;
    int ircport;
    struct sockaddr_in addr;
    struct sockaddr_in6 addr6;
};

struct xaddress_t {
    char *ircserver;              // Zachowujemy dla kompatybilności
    int ircport;                  // Zachowujemy dla kompatybilności
    struct sockaddr_in addr;      // Zachowujemy dla kompatybilności
    struct sockaddr_in6 addr6;    // Zachowujemy dla kompatybilności
    struct server_info *servers;  // Nowe pole - tablica serwerów
    int num_servers;              // Liczba serwerów
    int current_server;           // Aktualny indeks serwera
    char *ident_file, *ident_org, *log_file;
    int delay, timer, connecting, ident_oidentd2, log_fd;
    struct sockaddr_in bncaddr;
    vhost *vhost, *vhosttail;
    char *bncserver, *bncpass; 
    int bncport;
};
extern xaddress xconnect;
extern struct timeval tv_ping, tv_log;
struct tm *tm_log;
extern double cping;
extern int def_takemode, xlastjoin, xrejointimer, xrejoindelay;
extern const char xalphaf[], xalpha[], xcrapf[], xcrap[], xident[];
extern const float lxalpf, lxalp, lxcrapf, lxcrap, lxident;

vhost *add_vhost(char *, struct sockaddr_in *);
vhost *add_vhost6(char *, struct sockaddr_in6 *);
vhost *find_vhost_by_name(char *);
vhost *find_vhost_by_fd(int);
void del_vhost_all(void);
void del_vhost(vhost *, int);
char *next_bnc(void);
struct sockaddr *next_vhost(void);
struct sockaddr_in6 *next_vhost6(void);
char *next_vhost6_name(void);
int get_vhosts(void);
int get_vhost_add(struct in_addr);

void delete_connect_all(void);

void parse_input(void);
void do_connect(void);
void do_connect_bnc(void);

xfriend *add_friend(xchan *, char *);
xfriend *is_friend(xchan *, xnick *);
xfriend *find_friend(xchan *, char *);
void del_friend_nick(xchan *, xnick *);
void del_friend(xchan *, xfriend *);
int mask_match(char *, char *, char *);

char *random_nick(int);
char *make_nick(char *, int, int);
char* dict_nick(void);
void display_external_ipv6_addresses(void);
void add_all6host(void);
#endif
