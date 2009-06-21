# Copyright (c) 2009 Red Hat, Inc.
#
# Authors: Steven Dake (sdake@redhat.com)
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

AM_CFLAGS		= -fPIC $(corosync_CFLAGS)

INCLUDES		= -I$(top_builddir)/include -I$(top_srcdir)/include

src			= amfapp.c amfcluster.c amfnode.c amfsi.c amfutil.c \
			  amf.c amfcomp.c amfsg.c amfsu.c

SERVICE_LCRSO		= clm ckpt evt lck msg tmr

REGULAR_LCRSO		= openaisserviceenable

AMF_OBJECTS		= $(src:%.c=%.o)

SOURCES			= $(SERVICE_LCRSO:%=%.c) $(src) openaisserviceenable.c

EXTRA_DIST		= $(SOURCES) aisexec

LCRSO			= $(SERVICE_LCRSO:%=service_%.lcrso) $(REGULAR_LCRSO:%=%.lcrso) service_amf.lcrso

LCRSO_OBJS		= $(SOURCES:%.c=%.o)

noinst_HEADERS		= clm.h amf.h

sbin_SCRIPTS		= aisexec

sbin_PROGRAMS		= openais-instantiate

if BUILD_DARWIN
service_amf.lcrso: $(AMF_OBJECTS)
	$(CC) $(CFLAGS) -bundle -bundle_loader /usr/sbin/corosync $^ -o $@
service_%.lcrso: %.o
	$(CC) $(CFLAGS) -bundle -bundle_loader /usr/sbin/corosync $^ -o $@
%.lcrso: %.o
	$(CC) $(CFLAGS) -bundle -bundle_loader /usr/sbin/corosync $^ -o $@
endif
if BUILD_SOLARIS

service_amf.lcrso: $(AMF_OBJECTS)
	$(LD) $(LDFLAGS) -G $^ -o $@

service_%.lcrso: %.o
	$(LD) $(LDFLAGS) -G $^ -o $@

%.lcrso: %.o
	$(LD) $(LDFLAGS) -G $^ -o $@

else
service_amf.lcrso: $(AMF_OBJECTS)
	$(CC) $(CFLAGS) -shared -Wl,-soname=$@ $^ -o $@

service_%.lcrso: %.o
	$(CC) $(CFLAGS) -shared -Wl,-soname=$@ $^ -o $@

%.lcrso: %.o
	$(CC) $(CFLAGS) -shared -Wl,-soname=$@ $^ -o $@
endif

%.o: %.c
	$(CC) $(AM_CFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -c -o $@ $<

lint:
	-splint $(LINT_FLAGS) $(CFLAGS) *.c

all-local: $(LCRSO_OBJS) $(LCRSO)
	@echo Built Service Engines

install-exec-local:
	$(INSTALL) -d $(DESTDIR)/$(LCRSODIR)
	$(INSTALL) -m 755 $(LCRSO) $(DESTDIR)/$(LCRSODIR)

uninstall-local:
	cd $(DESTDIR)/$(LCRSODIR) && \
		rm -f $(LCRSO)

clean-local:
	rm -f *.o *.a *.so* *.da *.bb *.bbg *.lcrso
