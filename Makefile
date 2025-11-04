CC	=	gcc
NAME	=	enemy
LDFLAGS	=	-g
CFLAGS	=	-O2 -g -Wno-unused-result -D_DEFAULT_SOURCE -fcommon -fdebug-prefix-map=..=$(readlink -f ..)

OBJS	=	main.o clones.o irc.o parse.o action.o command.o proxy.o proxy_loader.o

all: $(NAME)

$(NAME): $(OBJS)
	rm -f $(NAME)
	$(CC) -O2 -Xlinker -g -o $(NAME) $(OBJS)

clean:
	rm -f $(OBJS) $(NAME)


