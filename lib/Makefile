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

override LDFLAGS += -L./ -L$(COROSYNCLIBDIR)

all: libSaClm.a libSaClm.so.3.0.0 \
	libSaCkpt.a libSaCkpt.so.3.0.0 \
	libSaAmf.a libSaAmf.so.3.0.0 \
	libSaEvt.a libSaEvt.so.3.0.0 \
	libSaMsg.a libSaMsg.so.3.0.0 \
	libSaLck.a libSaLck.so.3.0.0 \
	libSaTmr.a libSaTmr.so.3.0.0

ifeq (${OPENAIS_COMPAT}, DARWIN)

DARWIN_OPTS=-dynamiclib -bind_at_load -current_version 2.0.0 -compatibility_version 2.0.0

libSaAmf.so.3.0.0: util.o amf.o
	$(CC) $(DARWIN_OPTS) amf.o -o $@
	ln -sf libSaAmf.so.3.0.0 libSaAmf.so.3
	ln -sf libSaAmf.so.3.0.0 libSaAmf.so

libSaClm.so.3.0.0: util.o clm.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o clm.o -lcoroipcc -o $@
	ln -sf libSaClm.so.3.0.0 libSaClm.so.3
	ln -sf libSaClm.so.3.0.0 libSaClm.so

libSaCkpt.so.3.0.0: util.o ckpt.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o ckpt.o -lcoroipcc -o $@
	ln -sf libSaCkpt.so.3.0.0 libSaCkpt.so.3
	ln -sf libSaCkpt.so.3.0.0 libSaCkpt.so

libSaEvt.so.3.0.0: util.o evt.o 
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o evt.o -lcoroipcc -o $@
	ln -sf libSaEvt.so.3.0.0 libSaEvt.so.3
	ln -sf libSaEvt.so.3.0.0 libSaEvt.so

libSaMsg.so.3.0.0: util.o msg.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o msg.o -lcoroipcc -o $@
	ln -sf libSaMsg.so.3.0.0 libSaMsg.so.3
	ln -sf libSaMsg.so.3.0.0 libSaMsg.so

libSaLck.so.3.0.0: util.o lck.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o lck.o -lcoroipcc -o $@
	ln -sf libSaLck.so.3.0.0 libSaLck.so.3
	ln -sf libSaLck.so.3.0.0 libSaLck.so

libSaTmr.so.3.0.0: util.o tmr.o
	$(CC) $(LDFLAGS) $(DARWIN_OPTS) util.o tmr.o -lcoroipcc -o $@
	ln -sf libSaTmr.so.3.0.0 libSaTmr.so.3
	ln -sf libSaTmr.so.3.0.0 libSaTmr.so

else

libSaAmf.so.3.0.0: util.o amf.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname=libSaAmf.so.3,-version-script=$(srcdir)$(subdir)libSaAmf.versions util.o amf.o -lcoroipcc -o $@
	ln -sf libSaAmf.so.3.0.0 libSaAmf.so.3
	ln -sf libSaAmf.so.3.0.0 libSaAmf.so

libSaClm.so.3.0.0: util.o clm.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname=libSaClm.so.3,-version-script=$(srcdir)$(subdir)libSaClm.versions clm.o util.o -lcoroipcc -o $@
	ln -sf libSaClm.so.3.0.0 libSaClm.so.3
	ln -sf libSaClm.so.3.0.0 libSaClm.so

libSaCkpt.so.3.0.0: util.o ckpt.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname=libSaCkpt.so.3,-version-script=$(srcdir)$(subdir)libSaCkpt.versions util.o ckpt.o -lcoroipcc -o $@
	ln -sf libSaCkpt.so.3.0.0 libSaCkpt.so.3
	ln -sf libSaCkpt.so.3.0.0 libSaCkpt.so

libSaEvt.so.3.0.0: util.o evt.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname=libSaEvt.so.3,-version-script=$(srcdir)$(subdir)libSaEvt.versions util.o evt.o -lcoroipcc -o $@
	ln -sf libSaEvt.so.3.0.0 libSaEvt.so.3
	ln -sf libSaEvt.so.3.0.0 libSaEvt.so

libSaMsg.so.3.0.0: util.o msg.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname=libSaMsg.so.3,-version-script=$(srcdir)$(subdir)libSaMsg.versions util.o msg.o -lcoroipcc -o $@
	ln -sf libSaMsg.so.3.0.0 libSaMsg.so.3
	ln -sf libSaMsg.so.3.0.0 libSaMsg.so

libSaLck.so.3.0.0: util.o lck.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname=libSaLck.so.3,-version-script=$(srcdir)$(subdir)libSaLck.versions util.o lck.o -lcoroipcc -o $@
	ln -sf libSaLck.so.3.0.0 libSaLck.so.3
	ln -sf libSaLck.so.3.0.0 libSaLck.so

libSaTmr.so.3.0.0: util.o tmr.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname=libSaTmr.so.3,-version-script=$(srcdir)$(subdir)libSaTmr.versions util.o tmr.o -lcoroipcc -o $@
	ln -sf libSaTmr.so.3.0.0 libSaTmr.so.3
	ln -sf libSaTmr.so.3.0.0 libSaTmr.so

endif

libSaAmf.a: util.o amf.o
	$(AR) -rc libSaAmf.a util.o amf.o

libSaCkpt.a: util.o ckpt.o
	$(AR) -rc libSaCkpt.a util.o ckpt.o

libSaClm.a: util.o clm.o
	$(AR) -rc libSaClm.a util.o clm.o

libSaEvt.a: util.o evt.o
	$(AR) -rc libSaEvt.a util.o evt.o

libSaMsg.a: util.o msg.o
	$(AR) -rc libSaMsg.a util.o msg.o

libSaLck.a: util.o lck.o
	$(AR) -rc libSaLck.a util.o lck.o

libSaTmr.a: util.o tmr.o
	$(AR) -rc libSaTmr.a util.o tmr.o

clean:
	rm -f *.a *.o libSa*.so* *.da *.bb *.bbg

# -fPIC rules required for all libraries
%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

depend:
	makedepend -Y -- $(CFLAGS) $(CPPFLAGS) $(LIBAIS_SRC) > /dev/null 2>&1
