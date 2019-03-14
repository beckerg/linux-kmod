SUBDIRS = test linux

.PHONY: all ${SUBDIRS} ${MAKECMDGOALS}

all clean clobber cscope check debug load tags unload ${MAKECMDGOALS}: ${SUBDIRS}

${SUBDIRS}:
	${MAKE} -C $@ ${MAKECMDGOALS}
