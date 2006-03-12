GCC_WARNINGS1=-Wall -Wpointer-arith -Wstrict-prototypes
GCC_WARNINGS2=-Wmissing-prototypes -Wmissing-declarations
GCC_WARNINGS3=-Wnested-externs -fno-strict-aliasing
GCC_WARNINGS=$(GCC_WARNINGS1) $(GCC_WARNINGS2) $(GCC_WARNINGS3)
CFLAGS=-Os -g $(GCC_WARNINGS)
YFLAGS=-d

SRCS=gram.y lex.l cvs.h parsecvs.c cvsutil.c revlist.c atom.c revcvs.c

OBJS=gram.o lex.o parsecvs.o cvsutil.o revlist.o atom.o revcvs.o

parsecvs: $(OBJS)
	cc $(CFLAGS) -o $@ $(OBJS) $(LIBS)

$(OBJS): cvs.h
lex.o: y.tab.h

y.tab.h: gram.c

clean:
	rm -f $(OBJS) y.tab.h gram.c parsecvs
