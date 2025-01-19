# dzwm - dz window manager
# See LICENSE file for copyright and license details.

include config.mk

SRC = drw.c dzwm.c util.c
OBJ = ${SRC:.c=.o}

all: dzwm

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

config.h:
	cp config.def.h $@

dzwm: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f dzwm ${OBJ} dzwm-${VERSION}.tar.gz

dist: clean
	mkdir -p dzwm-${VERSION}
	cp -R LICENSE Makefile README config.def.h config.mk\
		drw.h util.h ${SRC} dzwm-${VERSION}
	tar -cf dzwm-${VERSION}.tar dzwm-${VERSION}
	gzip dzwm-${VERSION}.tar
	rm -rf dzwm-${VERSION}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f dzwm ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/dzwm

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/dzwm

.PHONY: all clean dist install uninstall
