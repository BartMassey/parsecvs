# Makefile for parsecvs
#
# Build requirements: A C compiler, yacc, lex, and asciidoc.

VERSION=0.2

GCC_WARNINGS1=-Wall -Wpointer-arith -Wstrict-prototypes
GCC_WARNINGS2=-Wmissing-prototypes -Wmissing-declarations
GCC_WARNINGS3=-fno-strict-aliasing -Wno-unused-function -Wno-unused-label
GCC_WARNINGS=$(GCC_WARNINGS1) $(GCC_WARNINGS2) $(GCC_WARNINGS3)
CFLAGS=-O2 -g $(GCC_WARNINGS) -DVERSION=\"$(VERSION)\"

# To enable debugging of the Yacc grammar, uncomment the following line
#CFLAGS += -DYYDEBUG=1

LIBS=-lssl -lcrypto -lz -lpthread
YFLAGS=-d -l
LFLAGS=-l

OBJS=gram.o lex.o parsecvs.o cvsutil.o revdir.o \
	revlist.o atom.o revcvs.o generate.o export.o \
	nodehash.o tags.o authormap.o

parsecvs: $(OBJS)
	cc $(CFLAGS) -o $@ $(OBJS) $(LIBS)

$(OBJS): cvs.h
lex.o: y.tab.h

lex.o: lex.c

y.tab.h: gram.c

.SUFFIXES: .html .asc .txt .1

# Requires asciidoc
.asc.1:
	a2x --doctype manpage --format manpage $*.asc
.asc.html:
	a2x --doctype manpage --format xhtml $*.asc

clean:
	rm -f $(OBJS) y.tab.h gram.c lex.c parsecvs docbook-xsl.css
install:
	cp parsecvs ${HOME}/bin

cppcheck:
	cppcheck --template gcc --enable=all -UUNUSED --suppress=unusedStructMember *.[ch]

SOURCES = Makefile *.[ch]
DOCS = README COPYING NEWS parsecvs.asc
ALL =  $(SOURCES) $(DOCS)
parsecvs-$(VERSION).tar.gz: $(ALL)
	tar --transform='s:^:parsecvs-$(VERSION)/:' --show-transformed-names -cvzf parsecvs-$(VERSION).tar.gz $(ALL)

dist: parsecvs-$(VERSION).tar.gz

release: parsecvs-$(VERSION).tar.gz parsecvs.html
	shipper -u -m -t; make clean; rm SHIPPER.FREECODE
