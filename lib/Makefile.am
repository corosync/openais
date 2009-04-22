#
# Copyright (c) 2009 Red Hat, Inc.
#
# Authors: Andrew Beekhof
#	   Steven Dake (sdake@redhat.com)
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

MAINTAINERCLEANFILES    = Makefile.in

AM_CFLAGS		= -fPIC $(coroipcc_CFLAGS)
AM_LDFLAGS		=

INCLUDES		= -I$(top_builddir)/include -I$(top_srcdir)/include

lib_LIBRARIES		= libSaAmf.a libSaCkpt.a libSaClm.a libSaEvt.a \
			  libSaLck.a libSaMsg.a libSaTmr.a

SHARED_LIBS		= $(lib_LIBRARIES:%.a=%.so.$(SONAME))
SHARED_LIBS_SO		= $(lib_LIBRARIES:%.a=%.so)
SHARED_LIBS_SO_TWO	= $(lib_LIBRARIES:%.a=%.so.$(SOMAJOR))

libSaAmf_a_SOURCES	= util.c amf.c
libSaCkpt_a_SOURCES	= util.c ckpt.c
libSaClm_a_SOURCES	= util.c clm.c
libSaEvt_a_SOURCES	= util.c evt.c
libSaLck_a_SOURCES	= util.c lck.c
libSaMsg_a_SOURCES	= util.c msg.c
libSaTmr_a_SOURCES	= util.c tmr.c

noinst_HEADERS		= util.h libSaAmf.versions libSaCkpt.versions \
			  libSaClm.versions libSaEvt.versions \
			  libSaLck.versions libSaMsg.versions libSaTmr.versions

if BUILD_DARWIN

libSaAmf.so.$(SONAME): util.o amf.o
	$(CC) $(DARWIN_OPTS) $(coroipcc_LIBS) $^ -o $@

libSaCkpt.so.$(SONAME): util.o ckpt.o
	$(CC) $(DARWIN_OPTS) $(coroipcc_LIBS) $^ -o $@

libSaEvt.so.$(SONAME): util.o evt.o
	$(CC) $(DARWIN_OPTS) $(coroipcc_LIBS) $^ -o $@

libSaLck.so.$(SONAME): util.o lck.o
	$(CC) $(DARWIN_OPTS) $(coroipcc_LIBS) $^ -o $@

libSaMsg.so.$(SONAME): util.o msg.o
	$(CC) $(DARWIN_OPTS) $(coroipcc_LIBS) $^ -o $@

libSaAmf.so.$(SONAME): util.o amf.o
	$(CC) $(DARWIN_OPTS) $(coroipcc_LIBS) $^ -o $@

libSaTmr.so.$(SONAME): util.o tmr.o
	$(CC) $(DARWIN_OPTS) $(coroipcc_LIBS) $^ -o $@

else

libSaAmf.so.$(SONAME): util.o amf.o
	$(CC) -shared -o $@ \
		-Wl,-soname=libSaAmf.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libSaAmf.versions \
		$^ $(LDFLAGS) $(coroipcc_LIBS)
	ln -sf libSaAmf.so.$(SONAME) libSaAmf.so
	ln -sf libSaAmf.so.$(SONAME) libSaAmf.so.$(SOMAJOR)

libSaCkpt.so.$(SONAME): util.o ckpt.o
	$(CC) -shared -o $@ \
		-Wl,-soname=libSaCkpt.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libSaCkpt.versions \
		$^ $(LDFLAGS) $(coroipcc_LIBS)
	ln -sf libSaCkpt.so.$(SONAME) libSaCkpt.so
	ln -sf libSaCkpt.so.$(SONAME) libSaCkpt.so.$(SOMAJOR)

libSaClm.so.$(SONAME): util.o clm.o
	$(CC) -shared -o $@ \
		-Wl,-soname=libSaClm.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libSaClm.versions \
		$^ $(LDFLAGS) $(coroipcc_LIBS)
	ln -sf libSaClm.so.$(SONAME) libSaClm.so
	ln -sf libSaClm.so.$(SONAME) libSaClm.so.$(SOMAJOR)

libSaEvt.so.$(SONAME): util.o evt.o
	$(CC) -shared -o $@ \
		-Wl,-soname=libSaEvt.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libSaEvt.versions \
		$^ $(LDFLAGS) $(coroipcc_LIBS)
	ln -sf libSaEvt.so.$(SONAME) libSaEvt.so
	ln -sf libSaEvt.so.$(SONAME) libSaEvt.so.$(SOMAJOR)

libSaLck.so.$(SONAME): util.o lck.o
	$(CC) -shared -o $@ \
		-Wl,-soname=libSaLck.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libSaLck.versions \
		$^ $(LDFLAGS) $(coroipcc_LIBS)
	ln -sf libSaLck.so.$(SONAME) libSaLck.so
	ln -sf libSaLck.so.$(SONAME) libSaLck.so.$(SOMAJOR)

libSaMsg.so.$(SONAME): util.o msg.o
	$(CC) -shared -o $@ \
		-Wl,-soname=libSaMsg.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libSaMsg.versions \
		$^ $(LDFLAGS) $(coroipcc_LIBS)
	ln -sf libSaMsg.so.$(SONAME) libSaMsg.so
	ln -sf libSaMsg.so.$(SONAME) libSaMsg.so.$(SOMAJOR)

libSaTmr.so.$(SONAME): util.o tmr.o
	$(CC) $(LDFLAGS) -shared -o $@ \
		-Wl,-soname=libSaTmr.so.$(SOMAJOR) \
		-Wl,-version-script=$(srcdir)/libSaTmr.versions \
		$^ $(LDFLAGS) $(coroipcc_LIBS)
	ln -sf libSaTmr.so.$(SONAME) libSaTmr.so
	ln -sf libSaTmr.so.$(SONAME) libSaTmr.so.$(SOMAJOR)

endif

all-local: $(SHARED_LIBS)
	@echo Built shared libs

install-exec-local:
	$(INSTALL) -d $(DESTDIR)/$(libdir)
	$(INSTALL) -m 755 $(SHARED_LIBS) $(DESTDIR)/$(libdir)
	cp -a $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO) $(DESTDIR)/$(libdir)

uninstall-local:
	cd $(DESTDIR)/$(libdir)/ && \
		rm -f $(SHARED_LIBS) $(SHARED_LIBS_SO) $(SHARED_LIBS_SO_TWO)

clean-local:
	rm -f *.o *.a *.so* *.da *.bb *.bbg
