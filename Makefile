# Copyright (c) 2002-2006 MontaVista Software, Inc.
# 
# All rights reserved.
# 
# This software licensed under BSD license, the text of which follows:
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# - Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# - Neither the name of the MontaVista Software, Inc. nor the names of its
#   contributors may be used to endorse or promote products derived from this
#   software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

# Production mode flags
CFLAGS = -O3 -Wall -fomit-frame-pointer
LDFLAGS = -lpthread

DESTDIR=/usr/local
SBINDIR=${DESTDIR}/usr/sbin
LIBDIR=${DESTDIR}/usr/lib
INCLUDEDIR=${DESTDIR}/usr/include
ETCDIR=${DESTDIR}/etc
ifeq (${DESTDIR},//)
MANDIR=/usr/share/man
else
MANDIR=$(DESTDIR)/man
endif

# Debug mode flags
#CFLAGS = -g -DDEBUG
#LDFLAGS = -g -lpthread

# Profile mode flags
#CFLAGS = -O3 -pg -DDEBUG
#LDFLAGS = -pg

all:
	(cd lib; echo ==== `pwd` ===; $(MAKE) all CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)");
	(cd exec; echo ==== `pwd` ===; $(MAKE) all CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)");
	(cd test; echo ==== `pwd` ===; $(MAKE) all CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)");

clean:
	(cd lib; echo ==== `pwd` ===; $(MAKE) clean);
	(cd exec; echo ==== `pwd` ===; $(MAKE) clean);
	(cd test; echo ==== `pwd` ===; $(MAKE) clean);

install:
	mkdir -p $(DESTDIR)/sbin
	mkdir -p $(SBINDIR)
	mkdir -p $(LIBDIR)
	mkdir -p $(INCLUDEDIR)
	mkdir -p $(ETCDIR)/ais
	mkdir -p $(MANDIR)/man3
	mkdir -p $(MANDIR)/man5
	mkdir -p $(MANDIR)/man8

	install -m 755 lib/libais.a $(LIBDIR)
	install -m 755 lib/libais.so* $(LIBDIR)
	install -m 755 lib/libSa*.a $(LIBDIR)
	install -m 755 lib/libSa*.so* $(LIBDIR)
	install -m 755 lib/libevs.a $(LIBDIR)
	install -m 755 lib/libevs.so* $(LIBDIR)
	install -m 755 exec/libtotem_pg* $(LIBDIR)

	install -m 755 exec/aisexec $(SBINDIR)
	install -m 755 exec/keygen $(SBINDIR)/ais-keygen
	install -m 755 conf/openais.conf $(ETCDIR)/openais.conf.picacho
	install -m 755 conf/groups.conf $(ETCDIR)/groups.conf.picacho

	install -m 755 include/saAis.h $(INCLUDEDIR)
	install -m 755 include/ais_amf.h $(INCLUDEDIR)
	install -m 755 include/saClm.h $(INCLUDEDIR)
	install -m 755 include/saCkpt.h $(INCLUDEDIR)
	install -m 755 include/saEvt.h $(INCLUDEDIR)
	install -m 755 include/evs.h $(INCLUDEDIR)
	install -m 755 exec/totem.h $(INCLUDEDIR)
	install -m 755 exec/aispoll.h $(INCLUDEDIR)
	install -m 755 exec/totempg.h $(INCLUDEDIR)

	install -m 755 man/evs_dispatch.3 $(MANDIR)/man3
	install -m 755 man/evs_fd_get.3 $(MANDIR)/man3
	install -m 755 man/evs_finalize.3 $(MANDIR)/man3
	install -m 755 man/evs_initialize.3 $(MANDIR)/man3
	install -m 755 man/evs_join.3 $(MANDIR)/man3
	install -m 755 man/evs_leave.3 $(MANDIR)/man3
	install -m 755 man/evs_mcast_groups.3 $(MANDIR)/man3
	install -m 755 man/evs_mcast_joined.3 $(MANDIR)/man3
	install -m 755 man/evs_membership_get.3 $(MANDIR)/man3
	install -m 755 man/evs_overview.8 $(MANDIR)/man8
	install -m 755 man/openais.conf.5 $(MANDIR)/man5
	install -m 755 man/openais_overview.8 $(MANDIR)/man8

	gzip -f -9 $(MANDIR)/man3/evs_dispatch.3
	gzip -f -9 $(MANDIR)/man3/evs_fd_get.3
	gzip -f -9 $(MANDIR)/man3/evs_finalize.3
	gzip -f -9 $(MANDIR)/man3/evs_initialize.3
	gzip -f -9 $(MANDIR)/man3/evs_join.3
	gzip -f -9 $(MANDIR)/man3/evs_leave.3
	gzip -f -9 $(MANDIR)/man3/evs_mcast_groups.3
	gzip -f -9 $(MANDIR)/man3/evs_mcast_joined.3
	gzip -f -9 $(MANDIR)/man3/evs_membership_get.3
	gzip -f -9 $(MANDIR)/man8/evs_overview.8
	gzip -f -9 $(MANDIR)/man5/openais.conf.5
	gzip -f -9 $(MANDIR)/man8/openais_overview.8

