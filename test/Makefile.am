#
# Copyright (C) 2009 Steven Dake
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#

MAINTAINERCLEANFILES    = Makefile.in
INCLUDES                = -I$(top_builddir)/include -I$(top_srcdir)/include

AM_CFLAGS		= $(coroipcc_CFLAGS) $(corosync_CFLAGS)
coro_LIBS		= $(coroipcc_LIBS)

noinst_PROGRAMS		= testckpt testevt testmsg testmsg2 testmsg3 testlck testlck2  testclm testtmr ckptbench testntf

noinst_HEADERS          = sa_error.h

testckpt_SOURCES	= testckpt.c sa_error.c
testckpt_LDADD		= -lSaCkpt
testckpt_LDFLAGS	= -L../lib $(coro_LIBS)

testevt_SOURCES		= testevt.c sa_error.c
testevt_LDADD		= -lSaEvt
testevt_LDFLAGS		= -L../lib $(coro_LIBS)

testmsg_SOURCES		= testmsg.c sa_error.c
testmsg_LDADD		= -lSaMsg
testmsg_LDFLAGS		= -L../lib $(coro_LIBS)

testmsg2_SOURCES	= testmsg2.c sa_error.c
testmsg2_LDADD		= -lSaMsg
testmsg2_LDFLAGS	= -L../lib $(coro_LIBS)

testmsg3_SOURCES	= testmsg3.c sa_error.c
testmsg3_LDADD		= -lSaMsg
testmsg3_LDFLAGS	= -L../lib $(coro_LIBS)

testlck_SOURCES		= testlck.c sa_error.c
testlck_LDADD		= -lSaLck
testlck_LDFLAGS		= -L../lib $(coro_LIBS)

testlck2_SOURCES	= testlck2.c sa_error.c
testlck2_LDADD		= -lSaLck
testlck2_LDFLAGS	= -L../lib $(coro_LIBS)

testclm_SOURCES		= testclm.c sa_error.c
testclm_LDADD		= -lSaClm
testclm_LDFLAGS		= -L../lib $(coro_LIBS)

testtmr_SOURCES		= testtmr.c sa_error.c
testtmr_LDADD		= -lSaTmr
testtmr_LDFLAGS		= -L../lib $(coro_LIBS)

ckptbench_SOURCES	= ckptbench.c sa_error.c
ckptbench_LDADD		= -lSaCkpt
ckptbench_LDFLAGS	= -L../lib $(coro_LIBS)

testntf_SOURCES		= testntf.c sa_error.c
testntf_LDADD		= -lSaNtf
testntf_LDFLAGS		= -L../lib $(coro_LIBS)

lint:
	-splint $(LINT_FLAGS) $(CFLAGS) *.c
