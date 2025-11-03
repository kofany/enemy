#ifndef __DEFS_H
#define __DEFS_H

// main.h a
#define XBSIZE		1537  	// 3 * 512 + 1
#define XHASH_SIZE      60      // 300 / 5
#define XHASH_CLONE     30      // 150 / 5

#define	COLOR_LEN	9
#define COLOR_BLACK	"[1;90;1m"
#define	COLOR_RED	"[1;91;1m"
#define COLOR_GREEN	"[1;92;1m"
#define COLOR_YELLOW	"[1;93;1m"
#define COLOR_BLUE	"[1;94;1m"
#define COLOR_PURPLE	"[1;96;1m"
#define COLOR_CYAN	"[1;91;1m"
#define COLOR_LCYAN	"[1;95;2m"
#define COLOR_WHITE	"[1;97;1m"
#define COLOR_NORLEN	4
#define COLOR_NORMAL	"[0m"

#define xdebug(l, c...) {			\
	if (l <= debuglvl) {			\
	(void)write(1, COLOR_BLACK, COLOR_LEN);	\
	(void)write(1, "[", 1);			\
	(void)write(1, COLOR_NORMAL, COLOR_NORLEN);	\
	(void)write(1, ">", 1);			\
	(void)write(1, COLOR_BLACK, COLOR_LEN);	\
	(void)write(1, "] ", 2);			\
	(void)write(1, COLOR_NORMAL, COLOR_NORLEN);	\
	printf(c); } }

#define info_printf(c...) { 			\
	(void)write(1, COLOR_BLACK, COLOR_LEN); 	\
	(void)write(1, "[", 1);			\
	(void)write(1, COLOR_CYAN, COLOR_LEN);	\
	(void)write(1, ">", 1);			\
	(void)write(1, COLOR_BLACK, COLOR_LEN);	\
	(void)write(1, "] ", 2);			\
	(void)write(1, COLOR_NORMAL, COLOR_NORLEN);	\
	printf(c); }

#define cdel_printf(c...) { 			\
	(void)write(1, COLOR_BLACK, COLOR_LEN); 	\
	(void)write(1, "[", 1);			\
	(void)write(1, COLOR_LCYAN, COLOR_LEN);	\
	(void)write(1, "-", 1);			\
	(void)write(1, COLOR_BLACK, COLOR_LEN);	\
	(void)write(1, "] ", 2);			\
	(void)write(1, COLOR_NORMAL, COLOR_NORLEN);	\
	printf(c); }

#define cadd_printf(c...) { 			\
	(void)write(1, COLOR_BLACK, COLOR_LEN); 	\
	(void)write(1, "[", 1);			\
	(void)write(1, COLOR_BLUE, COLOR_LEN);	\
	(void)write(1, "+", 1);			\
	(void)write(1, COLOR_BLACK, COLOR_LEN);	\
	(void)write(1, "] ", 2);			\
	(void)write(1, COLOR_NORMAL, COLOR_NORLEN);	\
	printf(c); }

#define cinfo_printf(c...) { 			\
	(void)write(1, COLOR_BLACK, COLOR_LEN); 	\
	(void)write(1, "[", 1);			\
	(void)write(1, COLOR_GREEN, COLOR_LEN);	\
	(void)write(1, ">", 1);			\
	(void)write(1, COLOR_BLACK, COLOR_LEN);	\
	(void)write(1, "] ", 2);			\
	(void)write(1, COLOR_NORMAL, COLOR_NORLEN);	\
	printf(c); }

#define help_printf(c...) { 			\
	(void)write(1, COLOR_BLACK, COLOR_LEN); 	\
	(void)write(1, "[", 1);			\
	(void)write(1, COLOR_WHITE, COLOR_LEN);	\
	(void)write(1, "?", 1);			\
	(void)write(1, COLOR_BLACK, COLOR_LEN);	\
	(void)write(1, "] ", 2);			\
	(void)write(1, COLOR_NORMAL, COLOR_NORLEN);	\
	printf(c); }

#define err_printf(c...) { 			\
	(void)write(1, COLOR_BLACK, COLOR_LEN); 	\
	(void)write(1, "[", 1);			\
	(void)write(1, COLOR_RED, COLOR_LEN);		\
	(void)write(1, "!", 1);			\
	(void)write(1, COLOR_BLACK, COLOR_LEN);	\
	(void)write(1, "] ", 2);			\
	(void)write(1, COLOR_NORMAL, COLOR_NORLEN);	\
	printf(c); }


typedef struct clone_t enemy; // clones.h
// irc.h

typedef struct channick_t xnick;
typedef struct chanclone_t xclone;
typedef struct channel_t xchan;
typedef struct chanmask_t xmask;
typedef struct chanmask_t xfriend;

#endif
