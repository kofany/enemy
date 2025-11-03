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
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include "defs.h"
#include "main.h"
#include "clones.h"
#include "irc.h"
#include "command.h"
#include "parse.h"
#include "action.h"

enemy *ping, *pingtail;
xclone *pobs;
#include "enemy.reasons"
int xpingreplies;
double lping, hping, aping;
void parse_clone(register enemy *p)
{
	register char 	*w, *nick = 0, *address = 0;
	register xclone *c = 0;
	register xchan 	*ch = 0;
	
	xdebug(6, "[%9s] %s\n", p->nick, read_str);
	
	// pierwsze 's³owo'
	w = newsplit(&read_str);
	
	// zapisujemy :asdasd!fahren@localhost.ds14.agh.edu.pl
	// nick		= asdasd
	// address	= fahren@localhost.ds14.agh.edu.pl
	if (*w == ':') {
		nick = w + 1;
		w = newsplit(&read_str);
		if ((address = strchr(nick, '!')))
			*address++ = 0;
	}
	
	if (!x_strcmp(w, "MODE")) { // DONE
		char mode = 0, *arg = 0;
		xnick *k, *j = 0;
		
		if ((ch = find_channel(newsplit(&read_str))) == 0 || \
			(c = find_clone(ch, p)) == 0 || c->kicked)
			return; // :IUsrkI MODE IUsrkI :+i

#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "!!! [%9s] Mode %s \"%s\" by %s!%s (ch->cnt=%d, c->cnt=%d+1, c->tcnt=%d)\n", p->nick, ch->name, read_str, nick, address, ch->cnt, c->cnt, c->tcnt);
#endif
		
		if (c->cnt < 0 ) {
			if (++c->tcnt == ch->cnt && c->cnt == -1) {
				c->cnt = c->tcnt;
				c->tcnt = 0;
			}
			return;
		} else if (++c->cnt > ch->cnt)
			ch->cnt = c->cnt;
		else
			return;
	
		if (xconnect.log_fd) {
			logit("Mode %s \"%s\" by %s!%s\n", ch->name, read_str, nick, address);
		}
		
		xdebug(3, "[%9s] Mode %s \"%s\" by %s%s%s!%s\n", p->nick, ch->name, read_str,
				COLOR_WHITE, nick, COLOR_NORMAL, address);
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "-> [%9s] Mode %s \"%s\" by %s!%s (ch->cnt=%d, c->cnt=%d, c->tcnt=%d)\n", p->nick, ch->name, read_str, nick, address, ch->cnt, c->cnt, c->tcnt);
#endif
		k = find_nick(ch, nick); // wykonuj±cy mode'a
		
		for (w = newsplit(&read_str); *w; w++)
			if (*w == '-')
				mode = 0;
			else if (*w == '+')
				mode = 1;
			else if (*w == 'o') { // hoho
				arg = newsplit(&read_str);
				if (mode) { // +o
					if ((c = find_clone_by_nick(ch, arg))) {
						if (c->op)
							continue;
						if (c->sentact_op) // o slicznie, ktos mial nas zaopowac
							del_sentop_clone(ch, c);
						else if (c->toop) // bylismy w kolejce do zaopowania
							del_toop_clone(ch, c);
						c->op = 1;
						ch->clonops++;
						if (c->enemy->pp < 6) { // hm, moze tu przesadzam
							c->ready = 1;
							ch->ready_clones++;
						}
#ifdef _ENEMY_DEBUG
						fprintf(fdebug, " + c->op of %s set to 1 (ready=%d) (clonops=%d, clones=%d, ready=%d)\n",
								c->enemy->nick, c->ready, ch->clonops, ch->clones, ch->ready_clones);
#endif
						if (parse_op < MAX_PARSE) {
							parse_mode[parse_op++] = c;
							parse_mode_chan = ch;
						}
					} else if ((j = find_nick(ch, arg))) {
						if (j->op)
							continue;
						if (j->sentact_op) // jw.
							del_sentop_nick(ch, j);
						else if (j->toop) // jw.
							del_toop_nick(ch, j);
						j->op = 1;
						ch->ops++;
						if (address == 0) { // nethack
							if (!(j->friend) && !(j->topun) && !(j->sentact_pun)) {
								add_topun(ch, j);
								parse_act = ch;
							}
						} else if (k && !(j->friend)) { // ktos zaopowal nie-frienda
							if (!(j->topun) && !(j->sentact_pun))
								add_topun(ch, j);
							if (!(k->friend) && !(k->topun) && !(k->sentact_pun) \
									&& k->op) // sprawdzamy przez tcnt
								add_topun(ch, k); // i to nie-friend !
							parse_act = ch;
						}
#ifdef _ENEMY_DEBUG
						fprintf(fdebug, " + n->op of %s set to 1 (ops=%d, users=%d)\n",
								j->nick, ch->ops, ch->users);
#endif

					}
				} else { // -o
					if ((c = find_clone_by_nick(ch, arg))) {
						if (c->op == 0) // i tak nie mial opa
							continue;
						c->op = 0;
						if (--(ch->clonops) == 0) {
							info_printf("Lost channel %s.\n", ch->name);
							ch->done = 0;
						}
						if (c->ready) {
							c->ready = 0;
							ch->ready_clones--;
						}
						add_toop_clone(ch, c); // reopujemy klona
						if (k && !(k->friend) && !(k->topun) && !(k->sentact_pun) && k->op)
							add_topun(ch, k); // jakis cwok zdeopowal klona
						parse_act = ch;
						del_sents_all(ch, c);	
#ifdef _ENEMY_DEBUG
						fprintf(fdebug, " - c->op of %s set to 0 (clonops=%d, clones=%d, ready=%d)\n",
								c->enemy->nick, ch->clonops, ch->clones, ch->ready_clones);
#endif
					} else if ((j = find_nick(ch, arg))) {
						if (j->op == 0) // i tak nie mial opa
							continue;
						j->op = 0;
						ch->ops--;
						if (j->friend && k && k != j) { // zdeopowali frienda
							add_toop_nick(ch, j);
							if (!(k->friend) && !(k->topun) && !(k->sentact_pun) && k->op)
								add_topun(ch, k); // przez jakiegos cwoka -- oczywiscie
							parse_act = ch;
						}
						if (j->sentact_pun) { // slicznie, akurat on mial byc wy*
							if (!k && j->sentact_pun->ready) {
								j->sentact_pun->ready = 0;
								ch->ready_clones--;
							}
							del_sentpun(ch, j);
						} else if (j->topun) // i tak czekal w kolejce
							del_topun(ch, j);
#ifdef _ENEMY_DEBUG
						fprintf(fdebug, " - n->op of %s set to 0 (ops=%d, users=%d)\n",
								j->nick, ch->ops, ch->users);
#endif
					}
				}
			} else if (*w == 'v') { // DONE
				arg = newsplit(&read_str);
				if ((j = find_nick(ch, arg)))
					j->voice = mode;
				else if ((c = find_clone_by_nick(ch, arg)))
					c->voice = mode;
			} else if (*w == 'b' || *w == 'I' || *w == 'e') { // DONE
				xmask *m;
				arg = newsplit(&read_str);
				m = find_mode(ch, *w, arg);
				if (mode && m == 0)
					add_mode(ch, *w, arg);
				else if (m)
					del_mode(ch, *w, m);
			} else if (*w == 'k') { // DONE
				if (ch->key) {
					free(ch->key);
					ch->key = 0;
				} else
					ch->key = strdup(newsplit(&read_str));
			} else if (*w == 'l') { // DONE
				if (mode)
					ch->limit = atoi(newsplit(&read_str));
				else if (ch->limit)
					ch->limit = 0;
				
				if (ch->pun_mode == 2 && ch->limit > -1 && k) { // klony moga wszystko (kanal closed)
					if (!(k->friend) && !(k->topun) && !(k->sentact_pun) && k->op) {
						add_topun(ch, k);
						parse_act = ch;
					}
					if (ch->clonops && (c = get_clone(ch))) {
						xsend(c->enemy->fd, "MODE %s +l -69\n", ch->name);
						c->enemy->pp += 4;
						if (c->ready) {
							c->ready = 0;
							ch->ready_clones--;
						}
					}
				}
			} else if (*w == 'i') // DONE
				ch->invite = mode;
	
		if (parse_op && !(ch->sents)) // pierwsze +ooo - wysylamy +o ASAP
			while (parse_op)
				clone_action(ch, parse_mode[--parse_op]);
						
		return;
	} else if (!x_strcmp(w, "KICK")) { // DONE
		xnick *k, *j;
		enemy *x;
	

		if ((ch = find_channel(newsplit(&read_str))) == 0)
			return;
		
		w = newsplit(&read_str); // nick kopanego
		
		if ((c = find_clone(ch, p)) == 0)
			return;
		
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "!!! [%9s] Kick for %s by %s!%s from %s (%s) (ch->cnt=%d, c->cnt=%d+1, c->tcnt=%d)\n",
				p->nick, w, nick, address, ch->name, read_str+1,
				ch->cnt, c->cnt, c->tcnt);
#endif

		k = find_nick(ch, nick); // kopi±cy
		
		if ((x = is_clone(w)) == p) {
			if (!(c->kicked)) { // pierwszy zobaczyl swojego kicka, huh
				if (xconnect.log_fd) {
					logit("Kick for %s by %s!%s from %s (%s)\n", w, nick, address,
							ch->name, read_str+1);
				}
				xdebug(3, "[%9s] Kick for %s%s%s by %s%s%s!%s from %s (%s)\n", p->nick, COLOR_WHITE, w,
					COLOR_NORMAL, COLOR_WHITE, nick, COLOR_NORMAL, address, ch->name, read_str+1);
#ifdef _ENEMY_DEBUG
				fprintf(fdebug, "-> [%9s] Kick for %s by %s!%s from %s (%s) (ch->cnt=%d, c->cnt=%d+1, c->tcnt=%d)\n",
					p->nick, w, nick, address, ch->name, read_str+1,
					ch->cnt, c->cnt, c->tcnt);
#endif
				if (k) {
					if (!(k->friend) && !(k->topun) && !(k->sentact_pun) && k->op) {
						// jakis ciec wyjebal klona
						add_topun(ch, k);
						parse_act = ch;
					}
					if (!(ch->invite) && (!(ch->limit) || ch->limit > (ch->clones + ch->users))) {
						xsend(p->fd, "JOIN %s %s\n", ch->name, ch->key? ch->key : "");
						p->pp += 3;
					} else if (ch->done == 2) {
						xclone *gc;
						if ((gc = get_clone(ch))) {
							xsend(gc->enemy->fd, "INVITE %s %s\n", w, ch->name);
							gc->enemy->pp += 4;
							if (gc->ready) {
								gc->ready = 0;
								ch->ready_clones--;
							}
						}
					} else
						add_rejoin(p, ch->name);
				}
				if (ch->cnt == c->cnt)
					ch->cnt++;
			} else
				c->kicked = 0;
				
			if (c->cnt != -1) // rejoin'al juz, 'omijamy' del_clone();
				del_clone(ch, c);

			return;
		}

		if (c->kicked)
			return;

		if (c->cnt < 0) {
			if (!(c->kicked) && ++c->tcnt == ch->cnt && c->cnt == -1) {
				c->cnt = c->tcnt;
				c->tcnt = 0;
			}
			return;
		} else if (++c->cnt > ch->cnt)
			ch->cnt = c->cnt;
		else
			return;
	
		if (xconnect.log_fd) {
			logit("Kick for %s by %s!%s from %s (%s)\n", w, nick, address,
					ch->name, read_str+1);
		}
		
		xdebug(3, "[%9s] Kick for %s%s%s by %s%s%s!%s from %s (%s)\n", p->nick, COLOR_WHITE, w,
			COLOR_NORMAL, COLOR_WHITE, nick, COLOR_NORMAL, address, ch->name, read_str+1);
	
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "-> [%9s] Kick for %s by %s!%s from %s (%s) (ch->cnt=%d, c->cnt=%d, c->tcnt=%d)\n",
				p->nick, w, nick, address, ch->name, read_str+1,
				ch->cnt, c->cnt, c->tcnt);
#endif
	
		if (x && (c = find_clone(ch, x))) { // wykopany zostal klon
			del_clone_fake(ch, c);
			if (k && !(k->friend) && !(k->topun) && !(k->sentact_pun) && k->op) {
				// jakis ciec wyjebal klona, k->op sprawdzamy przez tcnt
				add_topun(ch, k);
				parse_act = ch;
			} else if (!k) // klon wykopal klona, pewnie part/cycle :>
				return;
			if (!(ch->invite) && (!(ch->limit) || ch->limit > (ch->clones + ch->users))) {
				xsend(c->enemy->fd, "JOIN %s %s\n", ch->name, ch->key? ch->key : "");
				c->enemy->pp += 3;
			} else if (ch->done == 2) {
				xclone *gc;
				if ((gc = get_clone(ch))) {
					xsend(gc->enemy->fd, "INVITE %s %s\n", w, ch->name);
					gc->enemy->pp += 4;
					if (gc->ready) {
						gc->ready = 0;
						ch->ready_clones--;
					}
				}
			} else
				add_rejoin(c->enemy, ch->name);
		} else if ((j = find_nick(ch, w))) { // wykopany zostal nick :>
			if (j->friend) { // wyjebali kumpla :/
				if (k && !(k->friend) && !(k->topun) && !(k->sentact_pun) && k->op) {
					// w dodatku przez jakiegos ciecia :>
					add_topun(ch, k);
					parse_act = ch;
				}
				// inwajtujemy ciecia
				if ((ch->invite || (ch->limit && ch->limit < (ch->clones + ch->users))) && ch->clonops) {
					if ((c = get_clone(ch))) {
						xsend(c->enemy->fd, "INVITE %s %s\n", w, ch->name);
						c->enemy->pp += 4;
						if (c->ready) {
							c->ready = 0;
							ch->ready_clones--;
						}
					}
				}
			} else if (!k && j->sentact_pun && j->sentact_pun->ready) { // wywalal klon
				j->sentact_pun->ready = 0; // bo nie czyscimy tego w clone_action()
				ch->ready_clones--;
			}
			del_nick(ch, j);
		}
		return;
	} else if (!x_strcmp(w, "JOIN")) { // DONE
		register enemy *k = 0;
		register xclone *j = 0;
		register xnick *n = 0;
		

		w = (*read_str == ':')? read_str + 1 : read_str;
		k = is_clone(nick);
		
		if ((ch = find_channel(w)) == 0 && ((k == 0 || (ch = add_channel(w)) == 0)))
			return; // tworzymy kanal, jesli nie istnieje
				// a wszed³ jaki¶ klon

		if (k && (j = find_clone(ch, k)) == 0 && (j = add_clone(ch, k)) == 0)
			return;
	
		if (k == p)
			c = j;
		else if ((c = find_clone(ch, p)) == 0)
			return;
		
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "!!! [%9s] %s JOIN %s (%s) (ch->cnt=%d, c->cnt=%d+1, c->tcnt=%d, j->tcnt=%d)\n",
			p->nick, ch->name, nick, address, ch->cnt, c->cnt, c->tcnt, k? j->tcnt : -666);
#endif

		if (c->kicked)
			return;

		if (c->cnt < 0) {
			if (++c->tcnt == ch->cnt && c->cnt == -1) {
				c->cnt = c->tcnt;
				c->tcnt = 0;
			}
			return;
		} else {	
			if (k && j->cnt == -2) {
#ifdef _ENEMY_DEBUG
				fprintf(fdebug, "-> [%9s] %s Clone JOIN %s (%s) (ch->cnt=%d, c->cnt=%d, c->tcnt=%d, j->tcnt=%d)\n",
						p->nick, ch->name, nick, address, ch->cnt, c->cnt, c->tcnt, j->tcnt);
#endif

				if (xconnect.log_fd) {
					logit("%s %sJOIN %s (%s)\n", ch->name, (*read_str == ':')? "" : "Net", nick, address);
				}
				
				xdebug(3, "[%9s] %s %sJOIN %s%s%s (%s)\n",
						p->nick, ch->name, (*read_str == ':')? "" : "Net",
						COLOR_WHITE, nick, COLOR_NORMAL, address);
			

				if (!(j->toop) && !(j->sentact_op)) {
					add_toop_clone(ch, j);
				//	parse_act = ch;
				}
				
				if (j->tcnt == 1)  // zauwazyl tylko swojego join'a
					j->cnt = c->cnt + j->tcnt--;
				else {
					j->cnt = -1;
					j->tcnt += c->cnt;
				}
			}
			
			if (++c->cnt > ch->cnt)
				ch->cnt = c->cnt;
			else
				return;
			
			if (k) // add_clone() juz dawno polecial
				return;
		}

#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "-> [%9s] %s Nick JOIN %s (%s)\n", p->nick, ch->name, nick, address);
#endif
	
		if ((n = add_nick(ch, nick, address)) == 0)
			return;
		else if (is_friend(ch, n) == 0 && ch->pun_mode == 2) { // close
			add_topun(ch, n); // dopiero zostal stwozony, nie trzeba sprawdzac topun'a
			parse_act = ch;
		}
		
		if (xconnect.log_fd) {
			logit("%s %sJOIN %s (%s)\n", ch->name, (*read_str == ':')? "" : "Net", nick, address);
		}

		xdebug(3, "[%9s] %s %sJOIN %s%s%s (%s)\n", p->nick, ch->name, (*read_str == ':')? "" : "Net",
				COLOR_WHITE, nick, COLOR_NORMAL, address);
		
		return;
	} else if (!x_strcmp(w, "PRIVMSG")) { // DONE
		w = newsplit(&read_str);
		if (channame(w)) {
			if ((ch = find_channel(w)) == 0 || \
					(c = find_clone(ch, p)) == 0 || c->kicked)
				return;
			
			if (c->cnt < 0) {
				if (++c->tcnt == ch->cnt && c->cnt == -1) {
					c->cnt = c->tcnt;
					c->tcnt = 0;
				}
				return;
			} else if (++c->cnt > ch->cnt)
				ch->cnt = c->cnt;
			else
				return;
			if (xconnect.log_fd) {
				logit("<%s:%s> %s\n", nick, w, read_str+1);
			}
			xdebug(4, "[%9s] <%s%s%s:%s> %s\n",
						p->nick, COLOR_WHITE, nick, COLOR_NORMAL, w, read_str+1);
		} else {
			if (xconnect.log_fd) {
				logit("[%9s] [%s!%s] %s\n", p->nick, nick, address, read_str+1);
			}
			xdebug(2, "[%9s] [%s%s!%s%s] %s\n", p->nick, COLOR_WHITE, nick, address, COLOR_NORMAL, read_str+1);
		}
		
		return;
	} else if (!x_strcmp(w, "482")) { // DONE
		// 482    ERR_CHANOPRIVSNEEDED
		// :fahren.ds14.agh.edu.pl 482 fantazja #dupa :You're not channel operator
		
		if ((ch = find_channel(xnewsplit(2, &read_str))) == 0 || \
			(c = find_clone(ch, p)) == 0 || c->toop || c->sentact_op || c->cnt < 0)
			return; // juz wiadomo, ze nie ma opa
		
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "-> [%9s] %s (c->op=%d, ready=%d): %s\n", p->nick, ch->name, c->op, c->ready, read_str+1);
#endif

		if (!x_strcasecmp(xconnect.ircserver, nick)) {
			if (xconnect.log_fd) {
				logit("[%9s] %s: %s\n", p->nick, ch->name, read_str+1);
			}
			xdebug(1, "[%9s] %s%s%s: %s\n", p->nick, COLOR_WHITE, ch->name, COLOR_NORMAL, read_str+1);
		} else {
			if (xconnect.log_fd) {
				logit("[%9s] %s: %s (from %s)\n", p->nick, ch->name, read_str+1, nick);
			}
			xdebug(1, "[%9s] %s%s%s: %s (from %s%s%s)\n", p->nick,
					COLOR_WHITE, ch->name, COLOR_NORMAL, read_str+1,
					COLOR_WHITE, nick, COLOR_NORMAL);
			return;
		}

		if (c->op) {
			if (--(ch->clonops) == 0) {
				info_printf("Lost channel %s.\n", ch->name);
				ch->done = 0;
			}
			if (c->ready) {
				ch->ready_clones--;
				c->ready = 0;
			}
			c->op = 0;
		}
		
		// przerzucamy wszystkich, ktorych klon mial zaopowac jeszcze raz do kolejek	
		add_toop_clone(ch, c);
		del_sents_all(ch, c);
		
		parse_act = ch;
		return;
	} else if (!x_strcmp(w, "471") || !x_strcmp(w, "473") || !x_strcmp(w, "474")) { // DONE
		// 471    ERR_CHANNELISFULL
		// 473    ERR_INVITEONLYCHAN
		// 474    ERR_BANNEDFROMCHAN
		w = xnewsplit(2, &read_str);
		
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "-> [%9s] %s: %s\n", p->nick, w, read_str+1);
#endif
		if (xconnect.log_fd) {
			logit("[%9s] %s: %s\n", p->nick, w, read_str+1);
		}
		xdebug(2, "[%9s] %s%s%s: %s\n", p->nick, COLOR_WHITE, w, COLOR_NORMAL, read_str+1);
		if ((ch = find_channel(w)) && ch->done == 2 && (c = get_clone(ch))) {
			xsend(c->enemy->fd, "INVITE %s %s\n", p->nick, w);
			c->enemy->pp += 4;
			if (c->ready) {
				c->ready = 0;
				ch->ready_clones--;
			}
		} else
			add_rejoin(p, w);
		return;
	} else if (!x_strcmp(w, "475")) { // DONE
		// 475    ERR_BADCHANNELKEY
		w = xnewsplit(2, &read_str);
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "-> [%9s] %s: %s\n", p->nick, w, read_str+1);
#endif
		if (xconnect.log_fd) {
			logit("[%9s] %s: %s\n", p->nick, w, read_str+1);
		}
		xdebug(2, "[%9s] %s%s%s: %s\n", p->nick, COLOR_WHITE, w, COLOR_NORMAL, read_str+1);
		if ((ch = find_channel(w))) {
			xsend(p->fd, "JOIN %s %s\n", w, ch->key);
			p->pp += 3;
		} else
			add_rejoin(p, w);
		return;
	} else if (!x_strcmp(w, "PING")) { // DONE
		xsend(p->fd, "PONG %s\n", p->nick);
		p->pp++;
		xdebug(5, "[%9s] PING -> PONG\n", p->nick);
		return;
	} else if (!x_strcmp(w, "PONG")) { // DONE
		p->ping = p->pingparent = 0;
		
		gettimeofday(&tv_ping, 0);
		
		if (ping == 0) {
			lping = (tv_ping.tv_usec * 0.000001 + tv_ping.tv_sec);
			ping = pingtail = p; // pierwsze reply, zerujemy wszystko
			aping = 0.0;
			xpingreplies = 0;
			for (ch = chanroot; ch; ch = ch->next)
				ch->ping = ch->pingtail = 0;
		} else {
			pingtail->ping = p;
			p->pingparent = pingtail;
			pingtail = p;
		}
		
		xpingreplies++;
		aping += ((tv_ping.tv_usec * 0.000001 + tv_ping.tv_sec) - cping);
		
		for (ch = chanroot; ch; ch = ch->next)
			if ((c = find_clone(ch, p))) {
				c->ping = 0;
				if (ch->ping == 0)
					ch->ping = c;
				else
				 	ch->pingtail->ping = c;
				c->pingparent = ch->pingtail; // jak to bedzie pierwszy to bedzie NULL
				ch->pingtail = c;
			}
			
		if (xpingreplies < xcnt) // reply nie wrocilo do wszystkich jeszcze
			return;
	
		if (cping > 0) { // z command line'u (/ping)
			hping = (tv_ping.tv_usec * 0.000001 + tv_ping.tv_sec);
			if (xconnect.log_fd) {
				logit("<< Finished lagcheck. Ping min/avg/max = %.3f/%.3f/%.3f sec.\n",
					lping - cping, aping/xpingreplies, hping - cping);
			}
			cinfo_printf("Finished lagcheck. Ping min/avg/max = %.3f/%.3f/%.3f sec.\n",
					lping - cping, aping/xpingreplies, hping - cping);
		}
		
		for (ch = chanroot; ch; ch = ch->next) {
			ch->clone = ch->ping;
			ch->clonetail = ch->pingtail;
			for (c = ch->clone; c; c = c->ping) {
				c->next = c->ping;
				c->parent = c->pingparent;
			}
		}
	
		xpingreplies = -1; // main() przestawi glowna petle
		
		return;
	} else if (!x_strcmp(w, "332")) { // DONE
		// 332    RPL_TOPIC
		
		if ((ch = find_channel(xnewsplit(2, &read_str))) == 0 || \
				ch->synched != p->fd)
			return;
			
		if (xconnect.log_fd) {
			logit("%s TOPIC: %s\n", ch->name, read_str+1);
		}
		
		xdebug(4, "[%9s] Got %s's TOPIC.\n", p->nick, ch->name);
	
		ch->topic = strdup(read_str+1);

		return;
	} else if (!x_strcmp(w, "353")) { // done
		// 353    rpl_namreply
		register xclone *maybe = 0;
		register xnick *n = 0;
		enemy *x;
		w = xnewsplit(3, &read_str);
	
		if ((ch = find_channel(w)) == 0 || ch->synched != p->fd)
			return;
		
		read_str++;
	
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "-> [%9s] %s => %s\n", p->nick, w, read_str);
#endif
		
		if (xconnect.log_fd) {
			logit("%s => %s\n", w, read_str);
		}
		
		xdebug(3, "[%9s] %s %s=>%s %s\n", p->nick, w, COLOR_WHITE, COLOR_NORMAL, read_str);
	
		for (w = read_str; *w; w++)
			;
		while (*--w == ' ') // przeskakujemy spacje i \0
			;
		*(w+1) = 0; // konczymy string
		
		while (w != read_str) {
			while (*w != ' ' && w != read_str)
				w--;
			if (w != read_str)
				*w++ = 0;
			if (*w == '@') {
				if ((x = is_clone(w+1))) {
					if ((maybe = find_clone(ch, x))) {
						if (maybe->toop)
							del_toop_clone(ch, maybe); // musi miec toop'a, no ale :/
					} else if ((maybe = add_clone(ch, x)) == 0)
						return;
					maybe->op = 1;
					ch->clonops++;
					break;
				} else if ((n = add_nick(ch, w+1, 0))) {
					n->op = 1;
					add_topun(ch, n); // by default dodajemy do wyjebania
					ch->ops++;
				}
			} else if (*w == '+') {
				if ((x = is_clone(w+1)) && \
					((maybe = find_clone(ch, x)) || (maybe = add_clone(ch, x)))) {
					maybe->voice = 1;
					break;
				} else if ((n = add_nick(ch, w+1, 0)))
					n->voice = 1;
			} else {
				if ((x = is_clone(w)) && \
					((maybe = find_clone(ch, x)) || (maybe = add_clone(ch, x))))
					break;
				else
					add_nick(ch, w, 0);
			}
		}
		if (maybe) // jak jest kilka razy 353 to sie 'maybe' NULL'uje :/
			pobs = maybe;
		return;
	} else if (!x_strcmp(w, "366")) { // DONE
		// 366    RPL_ENDOFNAMES
		if ((ch = find_channel(xnewsplit(2, &read_str))) == 0 || \
			ch->synched != p->fd)
				return;
	
		if (pobs == 0)	
			pobs = find_clone(ch, p);
	
		pobs->cnt = 1; // znaleziony pierwszy obs :>
		pobs->tcnt = 0;
		ch->synched = -4;

#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "-> [%9s] %s Clone JOIN %s (%s) (ch->cnt=%d, c->cnt=%d, c->tcnt=%d)\n",
				pobs->enemy->nick, ch->name, pobs->enemy->nick, pobs->enemy->address,
				ch->cnt, pobs->cnt, pobs->tcnt);
#endif

		if (xconnect.log_fd) {
			logit("%s JOIN %s (%s)\n", ch->name, pobs->enemy->nick, pobs->enemy->address);
		}
		
		xdebug(3, "[%9s] %s JOIN %s%s%s (%s)\n",
				pobs->enemy->nick, ch->name,
				COLOR_WHITE, pobs->enemy->nick, COLOR_NORMAL, pobs->enemy->address);

//		if (!(pobs->op))
//			add_toop_clone(ch, pobs);

		return;
	} else if (!x_strcmp(w, "324")) { // DONE
		// 324    RPL_CHANNELMODEIS "<channel> <mode> <mode params>"

		if ((ch = find_channel(xnewsplit(2, &read_str))) == 0 || \
			ch->synched == 1)
			return;
	
		if (xconnect.log_fd) {
			logit("%s MODE: %s\n", ch->name, read_str+1);
		}
		
		xdebug(4, "[%9s] Got %s's MODE.\n", p->nick, ch->name);
	
		if ((w = newsplit(&read_str))) {
			w++;
			if (strchr(w, 'l')) // limit jest zawsze przed key'em
				ch->limit = atoi(newsplit(&read_str));
			for (; *w; w++) {
				if (*w == 'k') {
					ch->key = strdup(newsplit(&read_str));
					if (ch->key == 0)
						err_printf("got_324()->strdup(): %s\n",
								strerror(errno));
					break;
						
				} else if (*w == 'i')
					ch->invite = 1;
			}
		}

		if (++ch->synched == 1)
			// moze zmiana na info_printf() ?
			info_printf("Channel %s was synched.\n", ch->name);
		
		return;
	} else if (!x_strcmp(w, "367")) { // DONE
		// 367    RPL_BANLIST "<channel> <banmask>"
		
		if ((ch = find_channel(xnewsplit(2, &read_str))) != 0 && \
				 (ch->synched != 1))
				add_mode(ch, 'b', read_str);
		
		return;
	} else if (!x_strcmp(w, "368")) { // DONE
		// 368    RPL_ENDOFBANLIST
		// "<channel> :End of channel ban list"
		if ((ch = find_channel(xnewsplit(2, &read_str))) == 0 || \
			ch->synched == 1)
				return;
		
		xdebug(4, "[%9s] Got %s's ban list.\n", p->nick, ch->name);
		
		if (++(ch->synched) == 1)
			info_printf("Channel %s was synched.\n", ch->name);
			
		return;
	} else if (!x_strcmp(w, "346")) { // DONE
		// 346    RPL_INVITELIST "<channel> <invitemask>"
		
		if ((ch = find_channel(xnewsplit(2, &read_str))) != 0 && \
			ch->synched != 1)
				add_mode(ch, 'I', read_str);
		
		return;
	} else if (!x_strcmp(w, "347")) { // DONE
		// 347    RPL_ENDOFINVITELIST
		// "<channel> :End of channel invite list"

		if ((ch = find_channel(xnewsplit(2, &read_str))) == 0 || \
			ch->synched == 1)
				return;
		
		xdebug(4, "[%9s] Got %s's invite list.\n", p->nick, ch->name);
		
		if (++ch->synched == 1)
			info_printf("Channel %s was synched.\n", ch->name);
			
		return;
	} else if (!x_strcmp(w, "348")) { // DONE
		// 348    RPL_EXCEPTLIST "<channel> <exceptionmask>"

		if ((ch = find_channel(xnewsplit(2, &read_str))) != 0 && \
				ch->synched != 1)
			add_mode(ch, 'e', read_str);
		
		return;
	} else if (!x_strcmp(w, "349")) { // DONE
		// 349    RPL_ENDOFEXCEPTLIST "
		// "<channel> :End of channel exception list"
		
		if ((ch = find_channel(xnewsplit(2, &read_str))) == 0 || \
			ch->synched == 1)
				return;
		
		xdebug(4, "[%9s] Got %s's exception list.\n",
				p->nick, ch->name);
		
		if (++ch->synched == 1)
			info_printf("Channel %s was synched.\n", ch->name);
			
		return;
	} else if (!x_strcmp(w, "352")) { // DONE
		// 352    RPL_WHOREPLY
		// "<channel> <user> <host> <server> <nick>
		// ( "H" / "G" > ["*"] [ ( "@" / "+" ) ]
		// :<hopcount> <real name>"
		register xnick *n;
		
		if ((ch = find_channel(xnewsplit(2, &read_str))) == 0 || \
			ch->synched == 1)
				return;
	
		w = newsplit(&read_str);
		address = newsplit(&read_str);
		
		if ((n = find_nick(ch, xnewsplit(2, &read_str))) == 0)
			return;
		
		free(n->address); // jakby sie komus zachcialo walnac jeszcze raz WHO
		n->address = (char *)malloc(strlen(w) + strlen(address) + 2); // +2 dla '@' i '\0'
		if (n->address == 0) {
			err_printf("got_352()->malloc(): %s\n", strerror(errno));
			return;
		}
		sprintf(n->address, "%s@%s", w, address);

		if (!(n->friend)) // bez sensu sprawdzanie tego :/
			is_friend(ch, n);
		
		// g³upi IRCNet :<
		//w = newsplit(&read_str);
		//if (strchr(w, '@') && strchr(w, '+'))
		//	n->voice = 1;

		return;
	} else if (!x_strcmp(w, "315")) { // DONE
		// 315    RPL_ENDOFWHO "<name> :End of WHO list"
			
		if ((ch = find_channel(xnewsplit(2, &read_str))) == 0 || \
			ch->synched == 1)
				return;
		
		xdebug(4, "[%9s] Got %s's WHO.\n", p->nick, ch->name);
		
		if (++ch->synched == 1)
			info_printf("Channel %s was synched.\n", ch->name);
		
		return;
	} else if (!x_strcmp(w, "NICK")) { // DONE
		enemy *k;
		int hash;
		
		w = newsplit(&read_str) + 1; // nowy nick
		
		if ((k = is_clone(nick))) { // klon zmienil nicka
			char *nn;
			enemy *h;
			
			if ((nn = strdup(w)) == 0) {
				err_printf("got_nick()->strdup(): %s\n", strerror(errno));
				kill_clone(k, 1);
				return;
			}
			free(k->nick); k->nick = nn;
			
			if (xconnect.log_fd) {
				logit("NICK %s!%s -> %s\n", nick, address, w);
			}
			
			xdebug(3, "[%9s] NICK %s%s%s!%s -> %s%s%s\n",
				p->nick, COLOR_WHITE, nick, COLOR_NORMAL,
				address, COLOR_WHITE, w, COLOR_NORMAL);
#ifdef _ENEMY_DEBUG
			fprintf(fdebug, "-> [%9s] NICK %s!%s -> %s\n",
					p->nick, nick, address, w);
#endif

			hash = hashU(k->nick, XHASH_CLONE);
			if (hash == k->hash)  // har har har !!
				return;
			
			if (k->h_next) // usuwamy sie z tablicy hashow
				k->h_next->h_parent = k->h_parent;
			if (h_clone[k->hash] == k)
				h_clone[k->hash] = k->h_next;
			else
				k->h_parent->h_next = k->h_next;
			// .. tylko po to, zeby sie do niej dodac na nowo :/
			if ((h = h_clone[hash]))
				h->h_parent = k;
			k->h_next = h;
			k->h_parent = 0;
			h_clone[hash] = k;
			// teraz update'ujemy ch->h_clone[]
			for (ch = chanroot; ch; ch = ch->next)
				if (!(ch->inactive) && (c = find_clone(ch, k)))
					update_clone(ch, c, hash);
			k->hash = hash;
		} else { // jakis luser
			int f = 0;
			xnick *n;
			
			hash = hashU(w, XHASH_SIZE);
			for (ch = chanroot; ch; ch = ch->next)
				if (!(ch->inactive) && (n = find_nick(ch, nick))) {
					if (f == 0) {
						if (xconnect.log_fd) {
							logit("NICK %s!%s -> %s\n", nick, address, w);
						}
						xdebug(3, "[%9s] NICK %s%s%s!%s -> %s%s%s\n",
								p->nick, COLOR_WHITE, nick, COLOR_NORMAL,
								address, COLOR_WHITE, w, COLOR_NORMAL);
#ifdef _ENEMY_DEBUG
						fprintf(fdebug, "-> [%9s] NICK %s!%s -> %s\n",
								p->nick, nick, address, w);
#endif
						f = 1;
					}
					update_nick(ch, n, w, hash);
				}
		}
		return;
	} else if (!x_strcmp(w, "INVITE")) { // DONE
		w = xnewsplit(2, &read_str) + 1;
		
		if ((ch = find_channel(w))) {
			xdebug(3, "[%9s] %s%s%s!%s invited me to %s.\n", p->nick, COLOR_WHITE, nick, COLOR_NORMAL, address, w);
			xsend(p->fd, "JOIN %s %s\n", ch->name, ch->key? ch->key : "");
			p->pp += 3;
		} else {
			if (xconnect.log_fd) {
				logit("[%9s] INVITE from %s!%s to %s\n", p->nick, nick, address, w);
			}
			xdebug(1, "[%9s] %s%s%s!%s invited me to %s.\n", p->nick, COLOR_WHITE, nick, COLOR_NORMAL, address, w);
		}
		return;	
	} else if (!x_strcmp(w, "PART")) { // DONE
		xnick *n;
		enemy *x;

		if ((ch = find_channel(newsplit(&read_str))) == 0 || \
			(c = find_clone(ch, p)) == 0)
				return;

#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "!!! [%9s] %s PART %s (%s) [%s] (ch->cnt=%d, c->cnt=%d+1, c->tcnt=%d)\n",
				p->nick, ch->name, nick, address, read_str+1, ch->cnt, c->cnt, c->tcnt);
#endif
		
		if ((x = is_clone(nick)) == p) {
			if (!(c->kicked)) { // pierwszy zauwazyl swojego PART'a
				if (xconnect.log_fd) {
					logit("%s PART %s (%s) [%s]\n", ch->name, nick, address, read_str+1);
				}
				xdebug(3, "[%9s] %s PART %s%s%s (%s) [%s]\n",
					p->nick, ch->name, COLOR_WHITE, nick, COLOR_NORMAL, address, read_str+1);
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "-> [%9s] %s PART %s (%s) [%s] (ch->cnt=%d, c->cnt=%d+1, c->tcnt=%d)\n",
				p->nick, ch->name, nick, address, read_str+1, ch->cnt, c->cnt, c->tcnt);
#endif
				if (ch->cnt == c->cnt)
					ch->cnt++;
			} else
				c->kicked = 0;

			if (c->cnt != -1) // nikt nie zauwazyl jego reJOIN'a
				del_clone(ch, c);
			return;
		}

		if (c->kicked)
			return;

		if (c->cnt < 0) {
			if (++c->tcnt == ch->cnt && c->cnt == -1) {
				c->cnt = c->tcnt;
				c->tcnt = 0;
			}
			return;
		} else if (++c->cnt > ch->cnt)
				ch->cnt = c->cnt;
		else
			return;

		if (xconnect.log_fd) {
			logit("%s PART %s (%s) [%s]\n", ch->name, nick, address, read_str+1);
		}
		
		xdebug(3, "[%9s] %s PART %s%s%s (%s) [%s]\n",
				p->nick, ch->name, COLOR_WHITE, nick, COLOR_NORMAL, address, read_str+1);
	
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "-> [%9s] %s PART %s (%s) [%s] (ch->cnt=%d, c->cnt=%d, c->tcnt=%d)\n",
				p->nick, ch->name, nick, address, read_str+1, ch->cnt, c->cnt, c->tcnt);
#endif

		if (x && (c = find_clone(ch, x)))
			del_clone_fake(ch, c);
		else if ((n = find_nick(ch, nick)))
			del_nick(ch, n);
		
		return;
	} else if (!x_strcmp(w, "QUIT")) { // DONE
		xnick *n;
		enemy *x;

		// FIXME: nie zwiekszamy licznikow, no bo nie umiemy
		if ((x = is_clone(nick))) {
			if (xconnect.log_fd) {
				logit("%s QUIT %s (%s) [%s]\n", ch->name, nick, address, read_str+1);
			}
			kill_clone(x, 1);
		} else
			for (ch = chanroot; ch; ch = ch->next)
				if (!(ch->inactive) && (n = find_nick(ch, nick))) {
#ifdef _ENEMY_DEBUG
					fprintf(fdebug, "-> [%9s] %s QUIT %s (%s) [%s]\n",
							p->nick, ch->name, nick, address, read_str+1);
#endif
					del_nick(ch, n);
					if (xconnect.log_fd) {
						logit("%s QUIT %s (%s) [%s]\n", ch->name, nick, address, read_str+1);
					}
					xdebug(3, "[%9s] %s QUIT %s (%s) [%s]\n",
							p->nick, ch->name, nick, address, read_str+1);
				}
					
		return;
	} else if (!x_strcmp(w, "484")) { // DONE
		// 484    ERR_RESTRICTED
		vhost *v;
		err_printf("%s%s%s!%s has restricted connection.\n", COLOR_WHITE, p->nick, COLOR_NORMAL, p->address);
		if (xconnect.bncserver) {
			if ((v = find_vhost_by_name(strchr(p->address, '@') + 1)))
				del_vhost(v, 1);
		} else if ((v = find_vhost_by_fd(p->fd)))
			del_vhost(v, 1);
		kill_clone(p, 1);
		return;
	} else if (!x_strcmp(w, "403") || !x_strcmp(w, "401")) { // DONE
		// 401    ERR_NOSUCHNICK
		// 403    ERR_NOSUCHCHANNEL
		if (x_strcasecmp(xconnect.ircserver, nick)) {
			w = xnewsplit(2, &read_str);
			if (xconnect.log_fd) {
				logit("[%9s] %s: %s (from %s)\n", p->nick, w,
						"No such nick/channel", nick);
						
			}
			xdebug(1, "[%9s] %s%s%s: %s (from %s%s%s)\n", p->nick,
					COLOR_WHITE, w, COLOR_NORMAL, "No such nick/channel",
					COLOR_WHITE, nick, COLOR_NORMAL);
		}
		return;
	} else if (!x_strcmp(w, "404")) { // DONE
		// 404    ERR_CANNOTSENDTOCHAN
		if ((ch = find_channel(xnewsplit(2, &read_str))) == 0 || \
				(c = find_clone(ch, p)) == 0)
			return;
		if ((c->op || c->voice) && x_strcasecmp(xconnect.ircserver, nick)) {
			if (xconnect.log_fd) {
				logit("[%9s] %s: %s (from %s)\n", p->nick, ch->name,
						"Cannot send to channel", nick);
			}
			xdebug(1, "[%9s] %s%s%s: %s (from %s%s%s)\n", p->nick,
				COLOR_WHITE, ch->name, COLOR_NORMAL, "Cannot send to channel",
				COLOR_WHITE, nick, COLOR_NORMAL);
		}
		return;
	} else if (!x_strcmp(w, "TOPIC")) { // DONE
		if ((ch = find_channel(newsplit(&read_str))) == 0 || \
			(c = find_clone(ch, p)) == 0 || c->kicked)
			return;
		
		if (c->cnt < 0) {
			if (++c->tcnt == ch->cnt && c->cnt == -1) {
				c->cnt = c->tcnt;
				c->tcnt = 0;
			}
			return;
		} else if (++c->cnt > ch->cnt)
			ch->cnt = c->cnt;
		else
			return;
		
		free(ch->topic);
		if (*(++read_str)) {
#ifdef _ENEMY_DEBUG
			fprintf(fdebug, "-> [%9s] Topic change on %s by %s!%s to: %s\n", p->nick,
					ch->name, nick, address, read_str);
#endif
			if (xconnect.log_fd) {
				logit("Topic change on %s by %s!%s to: %s\n", ch->name, nick,
						address, read_str);
			}
			xdebug(3, "[%9s] Topic change on %s%s%s by %s%s%s!%s to: %s\n", p->nick,
				COLOR_WHITE, ch->name, COLOR_NORMAL,
				COLOR_WHITE, nick, COLOR_NORMAL, address, read_str);
			ch->topic = strdup(read_str);
		} else {
#ifdef _ENEMY_DEBUG
			if (xconnect.log_fd) {
				logit("Topic unset on %s by %s!%s\n", ch->name, nick, address);
			}
			fprintf(fdebug, "-> [%9s] Topic unset on %s by %s!%s.\n", p->nick,
					ch->name, nick, address);
#endif
			xdebug(3, "[%9s] Topic unset on %s%s%s by %s%s%s!%s.\n", p->nick,
					COLOR_WHITE, ch->name, COLOR_NORMAL,
					COLOR_WHITE, nick, COLOR_NORMAL, address);
			ch->topic = 0;
		}
		return;
	} else if (!x_strcmp(w, "001")){ // DONE
		if (x_strcasecmp(xconnect.ircserver, nick)) {
			xdebug(1, "%s claims to be %s%s%s, updating name.\n",
					xconnect.ircserver, COLOR_WHITE, nick, COLOR_NORMAL);
			free(xconnect.ircserver);
			xconnect.ircserver = strdup(nick);
		}
		for (w = read_str; *w; w++)
			;
		w--; // bo jeste¶my na koñcu, tj '\0'
		while (*w != ' ')
			w--;
		
		p->address = strdup(strchr(w, '!') + 1);
		if (p->address == 0) {
			err_printf("got_001()->strdup(): %s\n",
					strerror(errno));
			kill_clone(p, 1);
		}
		
		p->connected = 3;
		xcnt++;
		p->pp = 5; // 5 ? skad ja to wytrzaslem? :>
		
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "-> 001: %s (%s) has joined IRC. (xcnt=%d, xall=%d, xcon=%d)\n",
				p->nick, p->address, xcnt, xall, xconnect.connecting);
#endif
		
		xdebug(1, "Clone %d/%d: %s%s%s (%s) has joined IRC.\n",
				xcnt, xall + xconnect.connecting,
				COLOR_WHITE, p->nick, COLOR_NORMAL, p->address);
		
		if (xcnt == xall && !xconnect.connecting) {
			if (xconnect.log_fd) {
				logit("<< All %d clones has been connected to %s.\n", xall, xconnect.ircserver);
			}
			cinfo_printf("All %d clones has been connected to %s.\n", xall, xconnect.ircserver);
			if (xconnect.ident_file) // przywracamy orginalnego ident'a
				set_ident(xconnect.ident_org, 0);
		}
		
		return;
	} else if (!x_strcmp(w, "432")) {
		// 432    ERR_ERRONEUSNICKNAME
		w = xnewsplit(2, &read_str);
		xdebug(1, "[%9s] %s%s%s: %s\n", p->nick, COLOR_WHITE, w, COLOR_NORMAL, read_str+1);
		return;
	} else if (!x_strcmp(w, "433") || !x_strcmp(w, "437")) { // DONE
		// 433    ERR_NICKNAMEINUSE
		// 437    ERR_UNAVAILRESOURCE
		w = xnewsplit(2, &read_str);
		if (channame(w)) {
			xdebug(2, "[%9s] %s: %s\n", p->nick, w, read_str+1);
			add_rejoin(p, w);
			return;
		}
		// nick zajêty / niedostêpny -- losujemy nowego
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "-> [%9s] %s: %s\n", p->nick, w, read_str+1);
#endif
		if (p->connected == 3) {
			xdebug(1, "[%9s] %s%s%s: %s\n",
				p->nick, COLOR_WHITE, w, COLOR_NORMAL, read_str+1);
		} else {
			xdebug(3, "[%9s] %s%s%s: %s\n",
				p->nick, COLOR_WHITE, w, COLOR_NORMAL, read_str+1);
			if ((w = strdup(random_nick(1))) == 0) {
				err_printf("got_433()->strdup(): %s\n",
						strerror(errno));
				kill_clone(p, 1);
			}
			free(p->nick); p->nick = w;
			xsend(p->fd, "NICK %s\n", p->nick);
		}
		return;
	} else if (!x_strcmp(w, "ERROR")) { // to re-do
		vhost *v;
#ifdef _ENEMY_DEBUG
		fprintf(fdebug, "-> [%9s] ERROR: %s\n", p->nick, read_str + 1);
#endif
		info_printf("%s%s%s!%s ERROR: %s\n", COLOR_WHITE, p->nick, COLOR_NORMAL, p->address? p->address : "", read_str + 1);
		if (strstr(read_str, "Too many host connections") || strstr(read_str, "K-lined:")) {
			if (xconnect.bncserver) {
				if ((address = strchr(read_str, '@')) == 0)
					return;
				if ((nick = strchr(address++, ']')))
					*nick = 0;
				if ((v = find_vhost_by_name(address)))
					del_vhost(v, 1);
			} else if ((v = find_vhost_by_fd(p->fd)))
				del_vhost(v, 1);
		}
		kill_clone(p, 1);
		return;	
	} else if (!x_strcmp(w, "NOTICE")) { // DONE
		if (p->connected == 3 || !(xconnect.bncserver))
			return; // juz jest polaczony / nie korzystamy z bnc
		
		newsplit(&read_str);
		if (!x_strncmp(read_str, ":Failed Connect", 15)) {
			info_printf("%s%s%s BNC-ERROR: Failed connection to %s%s%s:%d\n",
				COLOR_WHITE, p->nick, COLOR_NORMAL,
				COLOR_WHITE, xconnect.ircserver, COLOR_NORMAL, xconnect.ircport);
			kill_clone(p, 1);
		}
		return;
	}
		
	xdebug(5, "[%9s] %s %s\n", p->nick, w, read_str);
}
