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

MAINTAINERCLEANFILES    = Makefile.in config.h.in

OPENAIS_H		= saAis.h saAmf.h saCkpt.h saClm.h saEvt.h saLck.h saMsg.h saTmr.h

OPENAIS_INTERNAL_H	= ipc_amf.h ipc_ckpt.h ipc_clm.h ipc_evt.h ipc_lck.h ipc_msg.h mar_msg.h ipc_tmr.h mar_ckpt.h mar_clm.h mar_evt.h mar_lck.h mar_sa.h

EXTRA_DIST 		= $(noinst_HEADERS)

noinst_HEADERS          = $(OPENAIS_INTERNAL_H)

openais_includedir	= ${includedir}/openais

openais_include_HEADERS	= $(OPENAIS_H)
