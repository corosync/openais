# Copyright (c) 2002-2005 MontaVista Software, Inc.
# Copyright (c) 2006-2008 Red Hat, Inc.
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

# Include configuration
#
srcdir ?= $(CURDIR)/../
subdir ?= lib/

include $(srcdir)Makefile.inc

override LDFLAGS += -L./ -L/usr/lib/corosync

all: libSaClm.a libSaClm.so.2.0.0 \
	libSaCkpt.a libSaCkpt.so.2.0.0 \
	libSaAmf.a libSaAmf.so.2.0.0 \
	libSaEvt.a libSaEvt.so.2.0.0 \
	libSaMsg.a libSaMsg.so.2.0.0  \
	libSaLck.a libSaLck.so.2.0.0 \
	libSaTmr.a libSaTmr.so.2.0.0

ifeq (${OPENAIS_COMPAT}, DARWIN)

DARWIN_OPTS=-dynamiclib -bind_at_load -current_version 2.0.0 -compatibility_version 2.0.0

libSaAmf.so.2.0.0: util.o amf.o
	$(CC) $(DARWIN_OPTS) util.o amf.o -o $@

libSaClm.so.2.0.0: util.o clm.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o clm.o /usr/lib/corosync/libcoroipc.a -o $@

libSaCkpt.so.2.0.0: util.o ckpt.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o ckpt.o /usr/lib/corosync/libcoroipc.a -o $@

libSaEvt.so.2.0.0: util.o evt.o 
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o evt.o /usr/lib/corosync/libcoroipc.a -o $@

libSaMsg.so.2.0.0: util.o msg.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o msg.o /usr/lib/corosync/libcoroipc.a -o $@

libSaLck.so.2.0.0: util.o lck.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o lck.o /usr/lib/corosync/libcoroipc.a -o $@

libSaTmr.so.2.0.0: util.o tmr.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o tmr.o /usr/lib/corosync/libcoroipc.a -o $@

else

libSaAmf.so.2.0.0: util.o amf.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaAmf.so.2,-version-script=$(srcdir)$(subdir)libSaAmf.versions util.o amf.o /usr/lib/corosync/libcoroipc.a -o $@

libSaClm.so.2.0.0: util.o clm.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaClm.so.2,-version-script=$(srcdir)$(subdir)libSaClm.versions util.o clm.o /usr/lib/corosync/libcoroipc.a -o $@

libSaCkpt.so.2.0.0: util.o ckpt.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaCkpt.so.2,-version-script=$(srcdir)$(subdir)libSaCkpt.versions util.o ckpt.o /usr/lib/corosync/libcoroipc.a -o $@

libSaEvt.so.2.0.0: util.o evt.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaEvt.so.2,-version-script=$(srcdir)$(subdir)libSaEvt.versions util.o evt.o /usr/lib/corosync/libcoroipc.a -o $@

libSaMsg.so.2.0.0: util.o msg.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaMsg.so.2,-version-script=$(srcdir)$(subdir)libSaMsg.versions util.o msg.o /usr/lib/corosync/libcoroipc.a -o $@

libSaLck.so.2.0.0: util.o lck.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaLck.so.2,-version-script=$(srcdir)$(subdir)libSaLck.versions util.o lck.o /usr/lib/corosync/libcoroipc.a -o $@

libSaTmr.so.2.0.0: util.o tmr.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libSaTmr.so.2,-version-script=$(srcdir)$(subdir)libSaTmr.versions util.o tmr.o /usr/lib/corosync/libcoroipc.a -o $@

endif

libSaAmf.a: util.o amf.o
	$(AR) x /usr/lib/corosync/libcoroipc.a
	$(AR) -rc libSaAmf.a util.o amf.o coroipc.o

libSaCkpt.a: util.o ckpt.o
	$(AR) x /usr/lib/corosync/libcoroipc.a
	$(AR) -rc libSaCkpt.a util.o ckpt.o coroipc.o

libSaClm.a: util.o clm.o
	$(AR) x /usr/lib/corosync/libcoroipc.a
	$(AR) -rc libSaClm.a util.o clm.o coroipc.o

libSaEvt.a: util.o evt.o
	$(AR) x /usr/lib/corosync/libcoroipc.a
	$(AR) -rc libSaEvt.a util.o evt.o coroipc.o

libSaMsg.a: util.o msg.o
	$(AR) x /usr/lib/corosync/libcoroipc.a
	$(AR) -rc libSaMsg.a util.o msg.o coroipc.o

libSaLck.a: util.o lck.o
	$(AR) x /usr/lib/corosync/libcoroipc.a
	$(AR) -rc libSaLck.a util.o lck.o coroipc.o

libSaTmr.a: util.o tmr.o
	$(AR) x /usr/lib/corosync/libcoroipc.a
	$(AR) -rc libSaTmr.a util.o tmr.o coroipc.o

clean:
	rm -f *.o libSa*.so* libSa*.a *.da *.bb *.bbg

# -fPIC rules required for all libraries
%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

depend:
	makedepend -Y -- $(CFLAGS) $(CPPFLAGS) $(LIBAIS_SRC) > /dev/null 2>&1
