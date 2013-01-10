# Makefile for parsecvs

GCC_WARNINGS1=-Wall -Wpointer-arith -Wstrict-prototypes
GCC_WARNINGS2=-Wmissing-prototypes -Wmissing-declarations
GCC_WARNINGS3=-fno-strict-aliasing -Wno-unused-function -Wno-unused-label
GCC_WARNINGS=$(GCC_WARNINGS1) $(GCC_WARNINGS2) $(GCC_WARNINGS3)
CFLAGS=-O2 -g $(GCC_WARNINGS)

# To enable debugging of the Yacc grammar, uncomment the following line
#CFLAGS += -DYYDEBUG=1

LIBS=-lssl -lcrypto -lz -lpthread
YFLAGS=-d -l
LFLAGS=-l

OBJS=gram.o lex.o parsecvs.o cvsutil.o revdir.o \
	revlist.o atom.o revcvs.o generate.o export.o gitutil.o \
	nodehash.o tags.o tree.o authormap.o

parsecvs: $(OBJS)
	cc $(CFLAGS) -o $@ $(OBJS) $(LIBS)

$(OBJS): cvs.h
lex.o: y.tab.h

lex.o: lex.c

y.tab.h: gram.c

clean:
	rm -f $(OBJS) y.tab.h gram.c lex.c parsecvs
install:
	cp parsecvs edit-change-log ${HOME}/bin
