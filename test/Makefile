# Copyright (c) 2016 Greg Becker.  All rights reserved.
#
# $Id: GNUmakefile 378 2016-01-18 14:24:15Z greg $


# The only things you probably need to change in this makefile is PROG, SRC, HDR,
# LDLIBS, and VPATH.
#
PROG	:= prog

HDR	:=
SRC	:= main.c
#LDLIBS	:= -lm -lpthread -lrt
#LDLIBS	:= -L../lib -lclp

VPATH	:=

# You probably don't need to change anything below this line.

PLATFORM	:= $(shell uname -s | tr 'a-z' 'A-Z')
INCLUDE		:= -I. -I../lib
DEBUG		:= -g -O0 -DDEBUG
CPPFLAGS	:= -D${PLATFORM}
CFLAGS		+= -Wall -g -O2 ${INCLUDE}

OBJ		:= ${SRC:.c=.o}

CSCOPE_DIRS	?= . ${VPATH} /usr/src/lib /usr/src/sys/kern /usr/src/sys/sys


# Always delete partially built targets.
#
.DELETE_ON_ERROR:

.PHONY:	all clean clobber cscope check debug etags load tags unload


all: ${PROG}

clean:
	rm -f ${PROG} ${OBJ} *.core

distclean clobber: clean
	rm -f cscope.files cscope*.out TAGS

cscope: cscope.out

cscope.out: cscope.files ${HDR} ${SRC} GNUmakefile
	cscope -bukq

cscope.files: GNUmakefile
	find ${CSCOPE_DIRS} -name \*.[chsSyl] -o -name \*.cpp > $@

check:
	./${PROG} /dev/xx1 $$((128*1024*1024))

debug: CFLAGS+=${DEBUG}
debug: ${PROG}

tags etags: TAGS

TAGS: cscope.files
	cat cscope.files | xargs etags -a --members --output=$@


load:
	true

unload:
	true

# Use gmake's link rule to produce the target.
#
${PROG}: ${OBJ}
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@


# We make ${OBJ} depend on the makefile so that objects are rebuilt
# if the makefile changes.  Not perfect, but better to err on the
# side of caution.
#
${OBJ}: Makefile


main.o: main.c
