version = 1.20

#
# configure options
# =============================================================================

CONFIG_MYSQL ?= y
CONFIG_MQTT  ?= y
CONFIG_PROFILE ?= n


CC=$(CROSS)gcc
LD=$(CROSS)ld

CFLAGS  = -g -MMD -O1 -DVERSION="\"${version}\"" -fstack-protector
CFLAGS += -Wall -Wextra -Wshadow -Wformat -Wframe-larger-than=2048
CFLAGS += -D_GNU_SOURCE
#CFLAGS += -Wstack-usage=2048
CFLAGS-$(CONFIG_PROFILE) += -pg
CFLAGS += $(CFLAGS-y)

LDLIBS = `pkg-config --libs inih`
LDLIBS-$(CONFIG_MYSQL) += `mysql_config --libs`
LDLIBS-$(CONFIG_MQTT) += -lmosquitto
LDLIBS += $(LDLIBS-y)

OBJS   = log.o control.o frame.o config.o serial.o backend.o stats.o
OBJS-$(CONFIG_MYSQL) += mysql.o
OBJS-$(CONFIG_MQTT) += mqtt.o
OBJS  += $(OBJS-y)

all: edfinfod edfctl

edfinfod: edfinfo.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

mysql.o: CFLAGS += `mysql_config --cflags`
mysql.o: mysql.c

config.o: CFLAGS += -DEDFINFO_CONF="\"$(sysconfdir)/edfinfo.conf\""
config.o: config.c

edfctl: LDLIBS = `pkg-config --libs inih`
edfctl: edfctl.o log.o frame.o config.o	backend.o stats.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	-rm -f edfinfod edfctl *.[od]

distclean: clean
	-rm -f ${distdir}.tar.gz  *~

-include $(wildcard *.d)

.PHONY: clean distclean

cscope:
	find . -name '*.[chS]' | xargs cscope

#
# install targets
# =============================================================================

prefix = /usr
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
sbindir = ${exec_prefix}/sbin
datarootdir = ${prefix}/share
mandir = ${datarootdir}/man
sysconfdir=${prefix}/etc

DESTDIR := $(HOME)

install: all
	- mkdir -p "$(DESTDIR)$(bindir)"
	- install -s -m 755  edfctl $(DESTDIR)$(bindir)
	- mkdir -p "$(DESTDIR)$(sbindir)"
	- install -s -m 755  edfinfod $(DESTDIR)$(sbindir)
	- mkdir -p "$(DESTDIR)$(sysconfdir)"
	- install -m 644  edfinfo.conf $(DESTDIR)$(sysconfdir)
	- mkdir -p "$(DESTDIR)$(mandir)/man8"
	- install -m 644 edfinfod.8 $(DESTDIR)$(mandir)/man8
	- mkdir -p "$(DESTDIR)$(mandir)/man1"
	- install -m 644 edfctl.1 $(DESTDIR)$(mandir)/man1

uninstall:
	- rm -f $(DESTDIR)$(bindir)/edfctl
	- rm -f $(DESTDIR)$(sbindir)/edfinfod
	- rm -f $(DESTDIR)$(sysconfdir)/edfinfo.conf
	- rm -f $(DESTDIR)$(mandir)/man8/edfinfo.8
	- rm -f $(DESTDIR)$(mandir)/man1/edfctl.1


#
# package targets
# =============================================================================

FILES := Makefile COPYING README.md edfinfo.conf \
	edfinfo.c edfinfo.h edfinfod.8 edfctl.c edfctl.1 \
	frame.c frame.h log.c log.h mysql.c \
	control.c control.h config.c config.h serial.c serial.h \
	mqtt.c backend.c backend.h stats.c stats.h \
	tests/Makefile tests/edfinfo*

distdir = edfinfo-$(version)
dist:
	tar -zcf ${distdir}.tar.gz  --xform="s,^,${distdir}/," $(FILES)

deb: dist
	mv ${distdir}.tar.gz  ../edfinfo_$(version).orig.tar.gz
	rm -rf ../${distdir}
	tar -C .. -xvf ../edfinfo_$(version).orig.tar.gz
	cp -a debian ../${distdir}
	(cd ../${distdir} ; dpkg-buildpackage -us -uc)


test test_real: all
	make -C tests $@
