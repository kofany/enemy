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
 * SUCH DAMAGE..
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include "defs.h"
#include "main.h"
#include "clones.h"
#include "irc.h"
#include "action.h"
#include "parse.h"
#include "command.h"

int xcnt;
xchan *chanroot;
#include "hide.info"

// hashU() zre¿niete z ksi±¿ki z algorytmami :>
int hashU(char *v, int m)
{
	int h, a = 31415, b = 27183;
	for (h = 0; *v != 0; v++, a = a*b % (m-1))
		h = (a*h + *v) % m;
	return (h < 0)? (h + m) : h;
}

xchan *add_channel(char *name)
{
	xchan *p;

	for (p = chanroot; p; p = p->next)
		if (!x_strcasecmp(p->name, name)) {
			if (p->inactive) {
				xfriend *f = p->friend;
				char *n = p->name;
				memset(p, 0, sizeof(xchan));
				p->name = n;
				p->friend = f;
				p->cnt = 1;
				p->pun_mode = def_takemode;
			}
			return p;
		}
		
	p = (xchan *)malloc(sizeof(xchan));
	
	if (p == 0) {
		err_printf("add_channel()->malloc(): %s\n",
				strerror(errno));
		return 0;
	}

	
	memset(p, 0, sizeof(xchan));
	p->name = strdup(name);

	if (p->name == 0) {
		err_printf("add_channel()->strdup(): %s\n",
				strerror(errno));
		free(p);
		return 0;
	}
	
	
	p->cnt = 1;
	p->pun_mode = def_takemode;

	if (chanroot)
		chanroot->parent = p;
	p->next = chanroot;
	chanroot = p;

#ifdef _ENEMY_DEBUG
	fprintf(fdebug, "* add_channel(): %s (%p, parent=%p, next=%p)\n", name, p, p->parent, p->next);
#endif
	
	return p;
}

void del_channel(xchan *ch)
{
	int i;
	xclone *c;
	xnick *n;
	xmask *m;

#ifdef _ENEMY_DEBUG
	fprintf(fdebug, "* del_channel(): %s (%p, parent=%p, next=%p)\n", ch->name, ch, ch->parent, ch->next);
#endif
	
	// mode'y :/
	while ((m = ch->ban)) {
		ch->ban = m->next;
		free(m->mask); free(m);
	}
	while ((m = ch->inv)) {
		ch->inv = m->next;
		free(m->mask); free(m);
	}
	while ((m = ch->exc)) {
		ch->exc = m->next;
		free(m->mask); free(m);
	}
	// nicki
	for (i = 0; i < XHASH_SIZE; i++)
		while ((n = ch->nick[i])) {
			ch->nick[i] = n->next;
			free(n->nick); free(n->address);
			free(n);
		}
	// i klony
	while ((c = ch->clone)) {
		ch->clone = c->next;
		free(c);
	}

	// wywalamy kanal z listy
	
	cdel_printf("Removed %s from cache.\n", ch->name);

	free(ch->key); free(ch->topic);
	if (ch->friend)	// ocalamy ustawienia :>
		ch->inactive = 1;
	else {
		if (ch->next)
			ch->next->parent = ch->parent;
		if (chanroot == ch)
			chanroot = ch->next;
		else
			ch->parent->next = ch->next;
		free(ch->name); free(ch);
	}
	// .. i oglaszamy zwyciestwo
}
	
xchan *find_channel(char *name)
{
	xchan *p;
	for (p = chanroot; p; p = p->next)
		if (!(p->inactive) && !x_strcasecmp(p->name, name))
			return p;
	return 0;
}

xchan *find_channel_friend(char *name)
{
	xchan *p;
	for (p = chanroot; p; p = p->next)
		if (!x_strcasecmp(p->name, name))
			return p;
	return 0;
}

xclone *add_clone(xchan *ch, enemy *c)
{
	xclone *p, *h;
	
	// dodajemy na koniec listy - najbardziej zalagowane na koncu :>
	if (ch->clone == 0)
		p = ch->clone = (xclone *)malloc(sizeof(xclone));
	else
		p = ch->clonetail->next = (xclone *)malloc(sizeof(xclone));
	
	if (p == 0) {
		err_printf("add_clone()->malloc(): %s\n",
				strerror(errno));
		return 0;
	}
	
	memset(p, 0, sizeof(xclone));
	p->parent = ch->clonetail;
	ch->clonetail = p;

	p->enemy = c;
	p->cnt = -2; // temp cnt

	// if (root) root->parent = p;
	// p->next = root; root = p;
	if ((h = ch->h_clone[c->hash]))
		h->h_parent = p;
	p->h_next = h;
	ch->h_clone[c->hash] = p;
		
	ch->clones++;
	
#ifdef _ENEMY_DEBUG
	fprintf(fdebug, "* add_clone(): %s (%p, parent=%p) on %s (clones=%d, users=%d)\n",
			p->enemy->nick, p, p->parent, ch->name, ch->clones, ch->users);
#endif


	add_toop_clone(ch, p);
//	parse_act = ch;

	switch (ch->synching) {
		case 0: // stan zaraz po stworzeniu kanalu
			xdebug(4, "[%9s] Creating channel %s...\n",
					p->enemy->nick, ch->name);
			ch->synching = 1;
			pobs = 0; // wazne !
			ch->synched = p->enemy->fd;
			xdebug(4, "[%9s] Getting %s's mode.\n", p->enemy->nick, ch->name);
			xsend(p->enemy->fd, "MODE %s\n", ch->name);
			p->enemy->pp += 2;
			if (xlastjoin > 1) // glupie zalozenie, ze wszystkie klony wejda na kanal
				break;
		case 1:
			ch->synching++;
			xdebug(4, "[%9s] Getting %s's WHO.\n",
					p->enemy->nick, ch->name);
			xsend(p->enemy->fd, "WHO %s\n", ch->name);
			p->enemy->pp += 4;
			if (xlastjoin > 2)
				break;
		case 2:
			ch->synching++;
			xdebug(4, "[%9s] Getting %s's bans.\n",
					p->enemy->nick, ch->name);
			xsend(p->enemy->fd, "MODE %s b\n", ch->name);
			p->enemy->pp += 2;
			if (xlastjoin > 3)
				break;
		case 3:
			ch->synching++;
			xdebug(4, "[%9s] Getting %s's exceptions.\n",
					p->enemy->nick, ch->name);
			xsend(p->enemy->fd, "MODE %s e\n", ch->name);
			p->enemy->pp += 2;
			if (xlastjoin > 4)
				break;
		case 4:
			ch->synching++;
			xdebug(4, "[%9s] Getting %s's invites.\n",
					p->enemy->nick, ch->name);
			xsend(p->enemy->fd, "MODE %s I\n", ch->name);
			p->enemy->pp += 2;
			break;
	}
	
	return p;
}

void del_clone(xchan *ch, xclone *c)
{
#ifdef _ENEMY_DEBUG
	fprintf(fdebug, "* del_clone(): %s (%p, parent=%p, next=%p) on %s (clones=%d, users=%d)\n",
			c->enemy->nick, c, c->parent, c->next, ch->name, ch->clones, ch->users);
#endif

	if (--ch->clones == 0) {
		// to byl ostatni klon, usuwamy kanal
		del_channel(ch);
		return;
	}
	
	// usuwamy z listy channel->clone
	if (ch->clonetail == c)
		ch->clonetail = c->parent;
	else if (c->next)
		c->next->parent = c->parent;
	
	if (ch->clone == c)
		ch->clone = c->next;
	else
		c->parent->next = c->next;
	
	// to samo z channel->ping
	if (ch->pingtail == c)
		ch->pingtail = c->pingparent;
	else if (c->ping)
		c->ping->pingparent = c->pingparent;
	
	if (ch->ping == c)
		ch->ping = c->next;
	else if (c->pingparent)
		c->pingparent->ping = c->ping;
	
	// .. oraz z tablicy hashow
	if (c->h_next)
		c->h_next->h_parent = c->h_parent;
	if (ch->h_clone[c->enemy->hash] == c)
		ch->h_clone[c->enemy->hash] = c->h_next;
	else
		c->h_parent->h_next = c->h_next;

	if (c->op) { // ma opa
		int i;
		// przerzucamy wszystkich losi ktorych mielismy opnac/wywalic do kolejek
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, " - del_clone(): removing %s (ready=%d) from clonops on %s (clonops=%d, clones=%d, ready=%d)\n",
				c->enemy->nick, c->ready, ch->name, ch->clonops, ch->clones, ch->ready_clones);
#endif
		if (--(ch->clonops) == 0) {
			ch->done = 0;
			if (xconnect.log_fd) {
				logit("<< Lost channel %s\n", ch->name);
			}
			info_printf("Lost channel %s.\n", ch->name);
		}
		if (c->ready)
			ch->ready_clones--;
		del_sents_all(ch, c);
		parse_act = ch;
		for (i = 0; i < parse_op; i++)
			if (parse_mode[i] == c) {
				parse_mode[i] = 0;
				break;
			}
	} else if (c->sentact_op) // nie mamy opa, ale moze ktos mial nas zaopowac ? :>
		del_sentop_clone(ch, c);
	else if (c->toop) // nie mamy opa, wiec zapewne czekamy w kolejce
		del_toop_clone(ch, c);

	free(c);
}

void del_clone_fake(xchan *ch, xclone *c)
{

#ifdef _ENEMY_DEBUG
	fprintf(fdebug, "* del_clone_fake(): %s (%p, parent=%p, next=%p) on %s (clones=%d, users=%d)\n",
			c->enemy->nick, c, c->parent, c->next, ch->name, ch->clones, ch->users);
#endif

	if (c->op) { // ma opa
		// przerzucamy wszystkich losi ktorych mielismy opnac/wywalic do kolejek
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, " - del_clone_fake(): removing %s (ready=%d) from clonops on %s (clonops=%d, clones=%d, ready=%d)\n",
				c->enemy->nick, c->ready, ch->name, ch->clonops, ch->clones, ch->ready_clones);
#endif
		if (--(ch->clonops) == 0) {
			ch->done = 0;
			if (xconnect.log_fd) {
				logit("<< Lost channel %s\n", ch->name);
			}
			info_printf("Lost channel %s.\n", ch->name);
		}
		if (c->ready) {
			c->ready = 0;
			ch->ready_clones--;
		}
		del_sents_all(ch, c);
		parse_act = ch;
		c->op = 0;
	} else if (c->sentact_op) // nie mamy opa, ale moze ktos mial nas zaopowac ? :>
		del_sentop_clone(ch, c);
	else if (c->toop) // nie mamy opa, wiec zapewne czekamy w kolejce
		del_toop_clone(ch, c);

	c->voice = 0;	
	c->kicked = 1;
	c->cnt = -2;
	c->tcnt = 0;
}

xclone *find_clone(xchan *chan, enemy *x)
{
	xclone *p;
	for (p = chan->h_clone[x->hash]; p; p = p->h_next)
		if (p->enemy == x)
			return p;
	return 0;
}

xclone *find_clone_by_nick(xchan *chan, char *nick)
{
	xclone *p;
	
	for (p = chan->h_clone[hashU(nick, XHASH_CLONE)]; p; p = p->h_next)
		if (!x_strcasecmp(p->enemy->nick, nick))
			return p;
	return 0;
}

xclone *get_clone(xchan *ch)
{
	xclone *c = 0, *r = 0;

	for (c = ch->clone; c; c = c->next)
		if (c->op) {
			r = c;
			if (c->enemy->pp < 10)
				return c;
		}
		
	return r;
}

xnick *add_nick(xchan *ch, char *nick, char *address)
{
	xnick *p, *h;

	p = (xnick *)malloc(sizeof(xnick));

	if (p == 0) {
		err_printf("add_nick()->malloc(): %s\n", strerror(errno));
		return 0;
	}
	
	memset(p, 0, sizeof(xnick));
	p->nick = strdup(nick);
	
	if (p->nick == 0) {
		err_printf("add_nick()->strdup(): %s\n", strerror(errno));
		free(p);
		return 0;
	}
	
	if (address) {
		p->address = strdup(address);
		if (p->address == 0) {
			err_printf("add_nick()->strdup(): %s\n",
					strerror(errno));
			free(p->nick); free(p);
			return 0;
		}
	}

	p->hash = hashU(nick, XHASH_SIZE);
	if ((h = ch->nick[p->hash]))
		h->parent = p;
	p->next = h;
	ch->nick[p->hash] = p;
	
	ch->users++;

#ifdef _ENEMY_DEBUG
	fprintf(fdebug, "* add_nick(): %s (%p, next=%p) on %s (clones=%d, users=%d)\n",
			p->nick, p, p->next, ch->name, ch->clones, ch->users);
#endif
	
	return p;
}

void del_nick(xchan *ch, xnick *c)
{
	
#ifdef _ENEMY_DEBUG
	fprintf(fdebug, "* del_nick(): %s (%p, parent=%p, next=%p) on %s (clones=%d, users=%d)\n",
			c->nick, c, c->parent, c->next, ch->name, ch->clones, ch->users);
#endif
	
	// usuwamy z tablicy hashow
	if (c->next)
		c->next->parent = c->parent;
	if (ch->nick[c->hash] == c)
		ch->nick[c->hash] = c->next;
	else
		c->parent->next = c->next;

	if (c->op) {
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, " - del_nick(): removing %s from ops on %s (ops=%d, users=%d)\n",
				c->nick, ch->name, ch->ops, ch->users);
#endif
		ch->ops--;
		if (c->sentact_pun) // o, ktos juz go chce ujebac :>
			del_sentpun(ch, c);
		else if (c->topun)	// nikt mu nie wyslal -o, wiec moze siedzi w topun?
			del_topun(ch, c);
	} else if (c->friend) { // nie ma opa, jest friendem wiec pewnie sie znajduje w toop list :>
		if (c->sentact_op)  // ktos juz wyslal +o ?
			del_sentop_nick(ch, c);
		else if (c->toop) // nie... a wiec napewno czekamy grzecznie w kolejce
			del_toop_nick(ch, c);
	} else // nie ma opa, nie ma frienda - wszystko mozliwe :>
		if (c->sentact_pun) // ktos pragnie go wywalic (np. .mkick)
			del_sentpun(ch, c);
		else if (c->topun)	// nie? to moze siedzi dopiero w kolejce?
			del_topun(ch, c);
		else if (c->sentact_op) // no to w takim razie moze ktos pragnie go zaopowac? (.op)
			del_sentop_nick(ch, c);
		else if (c->toop) // no to ostatnia szansa.. siedzi w kolejce?
			del_toop_nick(ch, c);

	ch->users--;

	free(c->nick); free(c->address); free(c);
}

xnick *find_nick(xchan *chan, char *nick)
{
	xnick *p;
	for (p = chan->nick[hashU(nick, XHASH_SIZE)]; p; p = p->next)
		if (!x_strcasecmp(p->nick, nick))
			return p;
	return 0;
}

xnick *find_nick_slow(xchan *ch, char *nick)
{
	xnick *p;
	int i;
	for (i = 0; i < XHASH_SIZE; i++)
		for (p = ch->nick[i]; p; p = p->next)
			if (!x_strcasecmp(p->nick, nick))
				return p;
	return 0;
}

void update_clone(xchan *ch, xclone *c, int nhash)
{
	xclone *h;
	
	// usuwamy sie i dodajemy z/do tablicy hashow
	if (c->h_next)
		c->h_next->h_parent = c->h_parent;
	if (ch->h_clone[c->enemy->hash] == c)
		ch->h_clone[c->enemy->hash] = c->h_next;
	else
		c->h_parent->h_next = c->h_next;

	if ((h = ch->h_clone[nhash]))
		h->h_parent = c;
	c->h_next = h;
	c->h_parent = 0;
	ch->h_clone[nhash] = c;
}

void update_nick(xchan *ch, xnick *n, char *nnick, int nhash)
{
	xnick *h;
	char *nn;
	
	// update'ujemy nicka
	if ((nn = strdup(nnick)) == 0) {
		err_printf("update_nick()->strdup(): %s\n", strerror(errno));
		del_nick(ch, n);
		return;
	}
	free(n->nick); n->nick = nn;
	
	// usuwamy sie i dodajemy z/do tablicy hashow
	if (n->next)
		n->next->parent = n->parent;
	if (ch->nick[n->hash] == n)
		ch->nick[n->hash] = n->next;
	else
		n->parent->next = n->next;

	if ((h = ch->nick[nhash]))
		h->parent = n;
	n->next = h;
	n->parent = 0;
	ch->nick[nhash] = n;
	
	n->hash = nhash;

	// update'ujemy n->friend
	if (n->friend)
		del_friend_nick(ch, n);
	is_friend(ch, n);
}

/* todo-s */

void add_topun(xchan *ch, xnick *n)
{
	if (ch->topun)
		ch->topun->t_parent = n;
	n->t_next = ch->topun;
	ch->topun = n;
	
	n->topun = 1;
	ch->topuns++;
	
#ifdef _ENEMY_DEBUG
	fprintf(fdebug, " + Adding %s to %s's topun (%p, parent=%p, next=%p, ch->topuns=%d)\n",
			n->nick, ch->name, n, n->t_parent, n->t_next, ch->topuns);
#endif
	
}

void del_topun(xchan *ch, xnick *n)
{

#ifdef _ENEMY_DEBUG
	fprintf(fdebug, " - Removing %s from %s's topun (%p, parent=%p, next=%p, ch->topuns=%d)\n",
			n->nick, ch->name, n, n->t_parent, n->t_next, ch->topuns);
#endif

	if (n->t_next)
		n->t_next->t_parent = n->t_parent;
	
	if (ch->topun == n)
		ch->topun = n->t_next;
	else
		n->t_parent->t_next = n->t_next;

	n->t_next = n->t_parent = 0;
	n->topun = 0;
	ch->topuns--;
}

void add_toop_nick(xchan *ch, xnick *n)
{
	if (ch->n_toop)
		ch->n_toop->t_parent = n;
	n->t_next = ch->n_toop;
	ch->n_toop = n;
	
	n->toop = 1;
	ch->toops++;
	
#ifdef _ENEMY_DEBUG
	fprintf(fdebug, " + Adding %s to %s's n_toop (%p, parent=%p, next=%p, ch->toops=%d)\n",
			n->nick, ch->name, n, n->t_parent, n->t_next, ch->toops);
#endif
}

void del_toop_nick(xchan *ch, xnick *n)
{
	
#ifdef _ENEMY_DEBUG
	fprintf(fdebug, " - Removing %s from %s's n_toop (%p, parent=%p, next=%p, ch->toops=%d)\n",
			n->nick, ch->name, n, n->t_parent, n->t_next, ch->toops);
#endif
	
	if (n->t_next)
		n->t_next->t_parent = n->t_parent;
	
	if (ch->n_toop == n)
		ch->n_toop = n->t_next;
	else
		n->t_parent->t_next = n->t_next;

	n->t_next = n->t_parent = 0;
	n->toop = 0;
	ch->toops--;
}

void add_toop_clone(xchan *ch, xclone *c)
{
	if (ch->c_toop == 0)
		ch->c_toop = c;
	else
		ch->c_tooptail->t_next = c;

	c->t_parent = ch->c_tooptail;
	ch->c_tooptail = c;
	
	c->toop = 1;
	ch->toops++;

#ifdef _ENEMY_DEBUG
	fprintf(fdebug, " + Adding %s to %s's c_toop (%p, parent=%p, next=%p, ch->toops=%d)\n",
			c->enemy->nick, ch->name, c, c->t_parent, c->t_next, ch->toops);
#endif

}

void del_toop_clone(xchan *ch, xclone *c)
{

#ifdef _ENEMY_DEBUG
	fprintf(fdebug, " - Removing %s from %s's c_toop (%p, parent=%p, next=%p, ch->toops=%d)\n",
			c->enemy->nick, ch->name, c, c->t_parent, c->t_next, ch->toops);
#endif

	if (ch->c_tooptail == c)
		ch->c_tooptail = c->t_parent;
	else if (c->t_next)
		c->t_next->t_parent = c->t_parent;
	
	if (ch->c_toop == c)
		ch->c_toop = c->t_next;
	else
		c->t_parent->t_next = c->t_next;

	c->t_next = c->t_parent = 0;
	c->toop = 0;
	ch->toops--;
}

/* sent'y */

void del_sents_all(xchan *ch, xclone *c)
{
	xclone *x;
	xnick *n;

#ifdef _ENEMY_DEBUG
	fprintf(fdebug, " * del_sents_all(): %s from %s\n", c->enemy->nick, ch->name);
#endif
	
	while ((x = c->c_sentop)) {
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, " - Removing %s from %s's c_sentop (%p, parent=%p, next=%p, sents=%d)\n",
			x->enemy->nick, c->enemy->nick, x, x->s_parent, x->s_next, ch->sents);
#endif
		c->c_sentop = x->s_next;
		x->sentact_op = x->s_next = x->s_parent = 0;
		add_toop_clone(ch, x);
		ch->sents--;
	}
	while ((n = c->n_sentop)) {
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, " - Removing %s from %s's n_sentop (%p, parent=%p, next=%p, sents=%d)\n",
			n->nick, c->enemy->nick, n, n->s_parent, n->s_next, ch->sents);
#endif
		c->n_sentop = n->s_next;
		n->s_next = n->s_parent = 0;
		n->sentact_op = 0;
		add_toop_nick(ch, n);
		ch->sents--;
	}
	while ((n = c->n_sentpun)) {
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, " - Removing %s from %s's n_sentpun (%p, parent=%p, next=%p, sents=%d)\n",
			n->nick, c->enemy->nick, n, n->s_parent, n->s_next, ch->sents);
#endif
		c->n_sentpun = n->s_next;
		n->s_next = n->s_parent = 0;
		n->sentact_pun = 0;
		add_topun(ch, n);
		ch->sents--;
	}
}

void del_sentop_clone(xchan *ch, xclone *c)
{
	
#ifdef _ENEMY_DEBUG
	fprintf(fdebug, " - Removing %s from %s's c_sentop (%p, parent=%p, next=%p, sents=%d)\n",
		          c->enemy->nick, c->sentact_op->enemy->nick, c, c->s_parent, c->s_next, ch->sents);
#endif

	if (c->s_next)	
		c->s_next->s_parent = c->s_parent;
	
	if (c->sentact_op->c_sentop == c)
		c->sentact_op->c_sentop = c->s_next;
	else
		c->s_parent->s_next = c->s_next;

	c->sentact_op = c->s_next = c->s_parent = 0;

	ch->sents--;
}

void del_sentop_nick(xchan *ch, xnick *c)
{

#ifdef _ENEMY_DEBUG
	fprintf(fdebug, " - Removing %s from %s's n_sentop (%p, parent=%p, next=%p, sents=%d)\n",
		          c->nick, c->sentact_op->enemy->nick, c, c->s_parent, c->s_next, ch->sents);
#endif

	if (c->s_next)	
		c->s_next->s_parent = c->s_parent;
	
	if (c->sentact_op->n_sentop == c)
		c->sentact_op->n_sentop = c->s_next;
	else
		c->s_parent->s_next = c->s_next;

	c->s_next = c->s_parent = 0;
	c->sentact_op = 0;

	ch->sents--;
}

void del_sentpun(xchan *ch, xnick *c)
{

#ifdef _ENEMY_DEBUG
	fprintf(fdebug, " - Removing %s from %s's n_sentpun (%p, parent=%p, next=%p, sents=%d)\n",
		          c->nick, c->sentact_pun->enemy->nick, c, c->s_parent, c->s_next, ch->sents);
#endif

	if (c->s_next)	
		c->s_next->s_parent = c->s_parent;
	
	if (c->sentact_pun->n_sentpun == c)
		c->sentact_pun->n_sentpun = c->s_next;
	else
		c->s_parent->s_next = c->s_next;

	c->s_next = c->s_parent = 0;
	c->sentact_pun = 0;
	
	ch->sents--;
}

/* mode'y */

xmask *add_mode(xchan *ch, int type, char *mask)
{
	xmask *p = 0;

	p = (xmask *)malloc(sizeof(xmask));

	if (p == 0) {
		err_printf("add_mode()->malloc(): %s\n", strerror(errno));
		return 0;
	}
		
	memset(p, 0, sizeof(xmask));
	
	p->mask = strdup(mask);

	if (p->mask == 0) {
		err_printf("add_mode()->strdup(): %s\n", strerror(errno));
		free(p);
		return 0;
	}
	
	switch (type) {
		case 'b':	
			if (ch->ban)
				ch->ban->parent = p;
			p->next = ch->ban;
			ch->ban = p;
			break;
		case 'I':	
			if (ch->inv)
				ch->inv->parent = p;
			p->next = ch->inv;
			ch->inv = p;
			break;
		case 'e':
			if (ch->exc)
				ch->exc->parent = p;
			p->next = ch->exc;
			ch->exc = p;
			break;
	}
	
	ch->modes++;
	
	return p;
}

void del_mode(xchan *ch, int type, xmask *m)
{
	if (m->next)
		m->next->parent = m->parent;
	
	switch (type) {
		case 'b':	
			if (ch->ban == m)
				ch->ban = m->next;
			else
				m->parent->next = m->next;
			break;
		case 'I':
			if (ch->inv == m)
				ch->inv = m->next;
			else
				m->parent->next = m->next;
			break;
		case 'e':
			if (ch->exc == m)
				ch->exc = m->next;
			else
				m->parent->next = m->next;
			break;
	}

	free(m->mask); free(m);
	ch->modes--;
}

xmask *find_mode(xchan *ch, int type, char *mask)
{
	xmask *p;
	
	switch (type) {
		case 'b':
			for (p = ch->ban; p; p = p->next)
				if (!x_strcasecmp(p->mask, mask))
					return p;
			break;
		case 'I':
			for (p = ch->inv; p; p = p->next)
				if (!x_strcasecmp(p->mask, mask))
					return p;
			break;
		case 'e':
			for (p = ch->exc; p; p = p->next)
				if (!x_strcasecmp(p->mask, mask))
					return p;
			break;
	}
	
	return 0;
}
