CFLAGS=-g
YFLAGS=-d

SRCS=gram.y lex.l cvs.h parsecvs.c cvsutil.c revlist.c

OBJS=gram.o lex.o parsecvs.o cvsutil.o revlist.o

parsecvs: $(OBJS)
	cc -o $@ $(OBJS) $(LIBS)

$(OBJS): cvs.h
lex.o: y.tab.h

y.tab.h: gram.c

clean:
	rm -f $(OBJS) y.tab.h gram.c parsecvs