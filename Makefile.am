
SUBDIRS = lib src po test

check-smoke:
	$(MAKE) -C test check-smoke

MOSTLYCLEANFILES = *.log core FILE *~
CLEANFILES = *.log core FILE *~
DISTCLEANFILES = *.log core FILE *~ prototype.bak 
MAINTAINERCLEANFILES = *.log core FILE *~ Makefile.in configure config.h.in \
                       config.status aclocal.m4 config.guess config.rpath \
                       config.sub depcomp install-sh missing mkinstalldirs

EXTRA_DIST = doc/* \
             examples/* \
             ttg/* \
             contrib/* \
             gawm.desktop gawm.png

appsdir   = $(datadir)/applications
icondir   = $(datadir)/pixmaps
apps_DATA = gawm.desktop
icon_DATA = gawm.png

ACLOCAL_AMFLAGS = -I m4
