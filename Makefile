CFLAGS=-g
YFLAGS=-d

SRCS=gram.y lex.l cvs.h parsecvs.c

OBJS=gram.o lex.o parsecvs.o

parsecvs: $(OBJS)
	cc -o $@ $(OBJS) $(LIBS)

$(OBJS): cvs.h
lex.o: y.tab.h

y.tab.h: gram.c