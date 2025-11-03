#ifndef __IRC_H
#define __IRC_H

#define channame(c)     (*c == '#' || *c == '!' || *c == '&')

struct chanmask_t {
	char *mask;
	xmask *next, *parent;
};

struct chanclone_t {
	int op, voice, cnt, tcnt, toop, ready, kicked;
	enemy *enemy; // dane klonaa
	xclone *sentact_op; // kto mial nas zaopowac ?
	xclone *c_sentop; // jakim klonom chcemy dac +o ?
	xnick *n_sentpun, *n_sentop;	// kogo chcemy wyjebac/zaopowac ?
	xclone *h_next, *h_parent, 	// ch->h_clone[] 
		*s_next, *s_parent,	// sentop-s
		*t_next, *t_parent,	// toop-s
		*next, *parent, 	// ch->clone
		*ping, *pingparent;	// ch->ping
};

struct channick_t {
	int	op, voice, hash,
		topun, toop;	// czy jest w kolejkach toop/topun ?
	char *nick, *address;	// do free()
	xfriend *friend;
	xclone *sentact_op, *sentact_pun; // kto chce nas zaopowac/wywalic
	xnick *next, *parent,  	// ch->nick[]
	  *t_next, *t_parent, 	// toop-s / topun-s
	  *s_next, *s_parent;	// SENTOp-s / sentpun-s
};

struct channel_t{
	int 	cnt, synched, synching,
		limit, inactive, invite,
		toops, topuns, ready_clones,
		sents, done, pun_mode;
	char *name, *key, *topic;

	// klony - dodajemy na koniec, ostatni wszedl = zalagowany
	xclone *h_clone[XHASH_CLONE], 	// do wyszukiwania po nickach
		*clone, *clonetail, *ping, *pingtail;	// do reszty
	int clones, clonops;
	// osoby - do korzenia
	xnick *nick[XHASH_SIZE];
	int users, ops;
	// friendsy - dodajemy do korzenia, wiec nie ma tail'a
	xfriend *friend;	// przechowuja info o nicku i masce

	// kolejki todos klonow - dodajemy na koniec
	xclone *c_toop, *c_tooptail;
	// kolejki cieciow - dodajemy na poczatek
	xnick *n_toop, *topun;
	
	// chanmode'y - dodajemy od korzenia
	xmask *ban, *inv, *exc; // free()'ujemy wszystko
	int modes; // licznikow mode'ow
	
	xchan *next, *parent;
};

extern xchan *chanroot;
extern int xcnt;
extern char *hinfo[];
int hashU(char *, int);

xchan *add_channel(char *);
void del_channel(xchan *);
xchan *find_channel(char *);
xchan *find_channel_friend(char *);

xclone *add_clone(xchan *, enemy *);
xclone *find_clone(xchan *, enemy *);
xclone *find_clone_by_nick(xchan *, char *);
xclone *get_clone(xchan *);
void del_clone(xchan *, xclone *);
void del_clone_fake(xchan *, xclone *);

xnick *add_nick(xchan *, char *, char *);
xnick *find_nick(xchan *, char *);
xnick *find_nick_slow(xchan *, char *);
void del_nick(xchan *, xnick *);

void update_clone(xchan *, xclone *, int);
void update_nick(xchan *, xnick *, char *, int);

void add_topun(xchan *, xnick *);
void del_topun(xchan *, xnick *);

void add_toop_nick(xchan *, xnick *);
void del_toop_nick(xchan *, xnick *);
void add_toop_clone(xchan *, xclone *);
void del_toop_clone(xchan *, xclone *);

void del_sents_all(xchan *, xclone *);
void del_sentop_clone(xchan *, xclone *);
void del_sentop_nick(xchan *, xnick *);
void del_sentpun(xchan *, xnick *);

xmask *add_mode(xchan *, int, char *);
xmask *find_mode(xchan *, int, char *);
void del_mode(xchan *, int, xmask *);

#endif
