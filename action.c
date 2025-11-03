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
 * SUCH DAMAGE.x
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "defs.h"
#include "main.h"
#include "clones.h"
#include "parse.h"
#include "irc.h"
#include "action.h"
#include "command.h"

xchan *parse_act, *parse_mode_chan;
int parse_op;
xclone *parse_mode[MAX_PARSE];

#include "hide.realnames"

char mode_op[5];
char *mode_arg[5], *kick_arg[6];
void clone_action(xchan *ch, register xclone *c)
{
	register int 	op_limit = 0,
			pun_limit = 0;
	int 	tot_mode = 0,
		tot_kick = 0;
	char *reason = 0;
		
	xclone *x, *t_x;
	xnick *n, *t_n;

	if (!c || !(c->op) || c->enemy->pp > 9 || c->kicked)
		return;

#ifdef _ENEMY_DEBUG
	fprintf(fdebug, "* clone_action() for %s on %s (ch->toops=%d, ch->topuns=%d, ch->ready=%d, ch->pun_mode=%d) ",
			c->enemy->nick, ch->name, ch->toops, ch->topuns, ch->ready_clones, ch->pun_mode);
#endif
	
	if (ch->pun_mode && ch->topuns) { // kick / close
		if (ch->toops == 0) { // prosta sprawa, nie mamy juz kogo opowac
		// tak samo jak z mode'ami, z tym, ze mozemy kopnac o 1 wiecej
			op_limit = 0;
			if (c->enemy->pp < 3)
				pun_limit = 6; // KICK 1,2; KICK 3,4,5,6
			else if (c->enemy->pp < 6)
				pun_limit = 5; // KICK 1; KICK 2,3,4,5
			else
				pun_limit = 4; // KICK 1,2,3,4
		} else if ((ch->ready_clones - (ch->ready_clones > 5? 4 : 0)) * 5 < ch->topuns) {
			// musimy zaopowac cieci :/
			if (c->enemy->pp < 2) { // 0, 1
				op_limit = ch->toops;
				if (op_limit > 4) {
					op_limit = 5; pun_limit = 0; // MODE +oo; MODE +ooo
				} else if (op_limit == 4)
					pun_limit = 1; // KICK 1; MODE +o; MODE +ooo
				else if (op_limit == 3)
					pun_limit = 2; // KICK 1,2; MODE +ooo
				else if (op_limit == 2)
					pun_limit = 4; // MODE +oo; KICK 1,2,3,4
				else if (op_limit == 1)
					pun_limit = 5; // MODE +o; KICK 1; KICK 2,3,4,5
			} else if (c->enemy->pp == 2) {
				op_limit = ch->toops;
				if (op_limit > 4) {
					op_limit = 5; pun_limit = 0; // MODE +oo; MODE +ooo
				} else if (op_limit == 4)
					pun_limit = 0; // MODE +o; MODE +ooo
				else if (op_limit == 3)
					pun_limit = 2; // KICK 1,2; MODE +ooo
				else // 2, 1
					pun_limit = 4; // MODE +o[+o]; KICK 1,2,3,4
			} else if (c->enemy->pp < 6) { // 3, 4, 5
				op_limit = ch->toops;
				if (op_limit > 3) {
					op_limit = 4; pun_limit = 0; // MODE +o; MODE +ooo
				} else if (op_limit == 1)
					pun_limit = 4; // MODE +o; KICK 1,2,3,4
				else // 2, 3
					pun_limit = 1; // KICK 1; MODE +oo[+o]
			} else { // 6, 7, 8, 9
				op_limit = 3; pun_limit = 0; // MODE +ooo
			}
		} else { // musimy zaopowac przynajmniej jedna osobe :/
			if (c->enemy->pp < 2) {
				pun_limit = ch->topuns;
				if (pun_limit > 4) {
					op_limit = 1; pun_limit = 5; // MODE +o; KICK 1; KICK 2,3,4,5
				} else if (pun_limit == 2)
					op_limit = 3; // KICK 1,2; MODE +ooo
				else if (pun_limit == 1)
					op_limit = 4; // KICK 1; MODE +o; MODE +ooo
				else // 3, 4
					op_limit = 2; // MODE +oo; KICK 1,2,3,4
			} else if (c->enemy->pp == 2) {
				if (ch->topuns > 2) {
					op_limit = 2; pun_limit = 4; // MODE +oo; KICK 1,2[,3,4]
				} else { // 1, 2
					op_limit = 3; pun_limit = ch->topuns; // KICK 1[,2]; MODE +ooo
				}
			} else if (c->enemy->pp < 6) { // 3,4,5
				if (ch->topuns == 1) {
					op_limit = 3; pun_limit = 1; // KICK 1; MODE +ooo
				} else {
					op_limit = 1; pun_limit = 4; // MODE +o; KICK 1[,2,3,4]
				}
			} else { // 6,7,8,9
				op_limit = 3; pun_limit = 0; // MODE +ooo
			}
		}
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "(c->pp=%d, op_limit=%d, pun_limit=%d)\n", c->enemy->pp, op_limit, pun_limit);
#endif
		for (n = ch->topun; pun_limit && n; n = t_n, pun_limit--) {
			t_n = n->t_next;
			del_topun(ch, n);
			if (c->n_sentpun)
				c->n_sentpun->s_parent = n;
			n->s_next = c->n_sentpun;
			c->n_sentpun = n;
			n->sentact_pun = c;
#ifdef _ENEMY_DEBUG
			fprintf(fdebug, " + Adding %s to %s's n_sentpun (%p, s_parent=%p, s_next=%p)\n",
					n->nick, c->enemy->nick, n, n->s_parent, n->s_next);
#endif
			kick_arg[tot_kick++] = n->nick;
		}
	} else { // deop
		if (c->enemy->pp < 3)
			op_limit = 5;
		else if (c->enemy->pp < 6)
			op_limit = 4;
		else
			op_limit = 3;
		
		if ((ch->ready_clones - (ch->ready_clones > 5? 4 : 0)) * 4 < ch->topuns)
			pun_limit = op_limit - ch->toops; // moze byc ujemne !
		else
			pun_limit = op_limit - (ch->toops? 1 : 0);
		
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "(c->pp=%d, op_limit=%d, pun_limit=%d)\n", c->enemy->pp, op_limit, pun_limit);
#endif
		for (n = ch->topun; pun_limit > 0 && n; n = t_n, pun_limit--, op_limit--) {
			t_n = n->t_next;
			del_topun(ch, n);
			if (c->n_sentpun)
				c->n_sentpun->s_parent = n;
			n->s_next = c->n_sentpun;
			c->n_sentpun = n;
			n->sentact_pun = c;
#ifdef _ENEMY_DEBUG
			fprintf(fdebug, " + Adding %s to %s's n_sentpun (%p, s_parent=%p, s_next=%p)\n",
					n->nick, c->enemy->nick, n, n->s_parent, n->s_next);
#endif
			mode_op[tot_mode] = '-';
			mode_arg[tot_mode++] = n->nick;
		}
	}
	
	for (x = ch->c_toop; op_limit && x; x = t_x, op_limit--) {
		t_x = x->t_next;
		del_toop_clone(ch, x);
		if (c->c_sentop)
			c->c_sentop->s_parent = x;
		x->s_next = c->c_sentop;
		c->c_sentop = x;	
		x->sentact_op = c;
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, " + Adding %s to %s's c_sentop (%p, s_parent=%p, s_next=%p)\n",
				x->enemy->nick, c->enemy->nick, x, x->s_parent, x->s_next);
#endif
		mode_op[tot_mode] = '+';
		mode_arg[tot_mode++] = x->enemy->nick;
	}
	
	for (n = ch->n_toop; op_limit && n; n = t_n, op_limit--) {
		t_n = n->t_next;
		del_toop_nick(ch, n);
		if (c->n_sentop)
			c->n_sentop->s_parent = n;
		n->s_next = c->n_sentop;
		c->n_sentop = n;
		n->sentact_op = c;
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, " + Adding %s to %s's n_sentop (%p, s_parent=%p, s_next=%p)\n",
				n->nick, c->enemy->nick, n, n->s_parent, n->s_next);
#endif
		mode_op[tot_mode] = '+';
		mode_arg[tot_mode++] = n->nick;
	}
	
	ch->sents += (tot_mode + tot_kick);
	
	if (tot_mode) {
		c->enemy->pp += ((tot_mode > 3? 2 : 1) + (3 * tot_mode));
		if (c->ready && !(c->n_sentpun)) {
			ch->ready_clones--;
			c->ready = 0;
		}
	}

	xsendint = 0;
	if (tot_kick) {
		c->enemy->pp += ((tot_kick > 4? 2 : 1) + (3 * tot_kick));
		reason = random_reason();
		if (tot_kick == 1)
			xsendint = snprintf(buf, XBSIZE, "KICK %s %s :%s\n", ch->name, kick_arg[0], reason);
		else if (tot_kick == 2)
			xsendint = snprintf(buf, XBSIZE, "KICK %s %s,%s :%s\n", ch->name,
					kick_arg[0], kick_arg[1], reason);
	}
	
	switch (tot_mode) {
		case 5:
			xsendint += snprintf(buf+xsendint, XBSIZE-xsendint,
					"MODE %s %co%co %s %s\nMODE %s %co%co%co %s %s %s\n",
					ch->name, mode_op[0], mode_op[1], mode_arg[0], mode_arg[1],
					ch->name, mode_op[2], mode_op[3], mode_op[4],
					mode_arg[2], mode_arg[3], mode_arg[4]);
			break;
		case 4:
			xsendint += snprintf(buf+xsendint, XBSIZE-xsendint,
					"MODE %s %co %s\nMODE %s %co%co%co %s %s %s\n",
					ch->name, mode_op[0], mode_arg[0],
					ch->name, mode_op[1], mode_op[2], mode_op[3],
					mode_arg[1], mode_arg[2], mode_arg[3]);
			break;
		case 3:
			xsendint += snprintf(buf+xsendint, XBSIZE-xsendint,
					"MODE %s %co%co%co %s %s %s\n",
					ch->name, mode_op[0], mode_op[1], mode_op[2],
					mode_arg[0], mode_arg[1], mode_arg[2]);
			break;
		case 2:
			xsendint += snprintf(buf+xsendint, XBSIZE-xsendint,
					"MODE %s %co%co %s %s\n",
					ch->name, mode_op[0], mode_op[1], mode_arg[0], mode_arg[1]);
			break;
		case 1:
			xsendint += snprintf(buf+xsendint, XBSIZE-xsendint,
					"MODE %s %co %s\n",
					ch->name, mode_op[0], mode_arg[0]);
			break;
	}

	switch (tot_kick) {
		case 6:
			xsendint += snprintf(buf+xsendint, XBSIZE-xsendint,
					"KICK %s %s,%s :%s\nKICK %s %s,%s,%s,%s :%s\n",
					ch->name, kick_arg[0], kick_arg[1], reason,
					ch->name, kick_arg[2], kick_arg[3], kick_arg[4], kick_arg[5], reason);
			break;
		case 5:
			xsendint += snprintf(buf+xsendint, XBSIZE-xsendint,
					"KICK %s %s :%s\nKICK %s %s,%s,%s,%s :%s\n",
					ch->name, kick_arg[0], reason,
					ch->name, kick_arg[1], kick_arg[2], kick_arg[3], kick_arg[4], reason);
			break;
		case 4:
			xsendint += snprintf(buf+xsendint, XBSIZE-xsendint,
					"KICK %s %s,%s,%s,%s :%s\n",
					ch->name, kick_arg[0], kick_arg[1], kick_arg[2], kick_arg[3], reason);
			break;
		case 3:
			xsendint += snprintf(buf+xsendint, XBSIZE-xsendint,
					"KICK %s %s,%s,%s :%s\n",
					ch->name, kick_arg[0], kick_arg[1], kick_arg[2], reason);
			break;
	}

	if (xsendint)
		write(c->enemy->fd, buf, xsendint);
	else if (ch->done != 2 && !(ch->sents) && ch->clones == ch->clonops) {
		if (ch->pun_mode == 2 && ch->done != 1) { // ch->done == 0
			int i;
			if (ch->limit > -1) { // jesli dodatni limit / nie ma wogole limitu
				xsend(c->enemy->fd, "MODE %s +l -69\n", ch->name);
				c->enemy->pp += 4;
			}
			for (i = 0; i < XHASH_SIZE; i++)
				for (n = ch->nick[i]; n; n = n->next)
					if (!(n->friend)) // nie moze byc w topun bo by tu nie doszla akcja
						add_topun(ch, n);
			parse_act = ch;
			ch->done = 1;
//			if (c->enemy->pp > 9) // nie moze ustawic topicu - no to next
				return;
		}
//		if (!(ch->topic) || x_strcmp(ch->topic, xreasons[0].string)) {
//			xsend(c->enemy->fd, "TOPIC %s :%s\n", ch->name, xreasons[0].string);
//		} else {
//			int i = xrand(lxreasons); // ustawia randomowy, inny niz '0' reason
//			xsend(c->enemy->fd, "TOPIC %s :%s\n", ch->name, xreasons[(i == 0)? 1 : i].string);
//		}
		c->enemy->pp += 4;
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "+ Finished action on %s (%d=clonops, %d=ops).\n",
			ch->name, ch->clonops, ch->ops);
#endif
		ch->done = 2;
		if (xconnect.log_fd) {
			logit("<< Finished action on %s\n", ch->name);
		}
		cinfo_printf("Finished action on %s.\n", ch->name);
	}
}

void chan_action(xchan *ch)
{
	xclone *c;

	if (!(ch->clonops))
		return;
	
	for (c = ch->clone; c; c = c->next)
		if (ch->c_toop || ch->n_toop || ch->topun || ch->done != 2)
			clone_action(ch, c);
		else
			return;
}
