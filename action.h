#ifndef __ACTION_H
#define __ACTION_H

#define MAX_PARSE	20 // tyle +o maxymalnie parsujemyy

extern xchan *parse_act, *parse_mode_chan;
extern int parse_op;
extern xclone *parse_mode[];
extern char *hrealnames[];

void chan_action(xchan *);
void clone_action(xchan *, xclone *);

#endif
