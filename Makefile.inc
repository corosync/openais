# Copyright (c) 2006-2007 Red Hat, Inc.
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
# Basic OS detection
#
CP=cp
DESTDIR=
PREFIX=/usr/local
UNAME=$(shell uname)
LCRSODIR=$(PREFIX)/libexec/lcrso

ifeq "$(UNAME)" "Linux"
	OPENAIS_COMPAT=LINUX
endif
ifeq "$(UNAME)" "Darwin"
	CP=rsync
	OPENAIS_COMPAT=DARWIN
endif
ifneq "" "$(findstring BSD,$(UNAME))"
	OPENAIS_COMPAT=BSD
endif
ifndef OPENAIS_COMPAT
$(error "OPENAIS_COMPAT cannot be detected, it must be manually defined")
endif

# BUILD_DYNAMIC can be defined to 1 to build for dynamic loading of service
#	handler modules.  If the developer intends to debug, building without
#	dynamic modules should provide an easier route.
ifndef BUILD_DYNAMIC
	BUILD_DYNAMIC=1
endif

# OPENAIS_BUILD can be defined as RELEASE or DEBUG
#
ifndef OPENAIS_BUILD
	OPENAIS_BUILD=RELEASE
endif

# OPENAIS_RT can be defined to 1 to build with librt
ifndef LINK_WITH_RT
	LINK_WITH_RT=1
endif

# OPENAIS_PROFILE

# default CFLAGS, LDFLAGS
#
CFLAGS = 
LDFLAGS =
DYFLAGS =

# build CFLAGS, LDFLAGS
#
ifeq (${OPENAIS_BUILD}, RELEASE) 
	CFLAGS += -O3 -Wall
# -Wstrict-aliasing=2 TODO sameday fix all of these
ifndef OPENAIS_PROFILE
	CFLAGS += -fomit-frame-pointer
endif
	LDFLAGS += 
endif
ifeq (${OPENAIS_BUILD}, DEBUG) 
	CFLAGS += -O0 -g -Wall -DDEBUG 
	LDFLAGS += -g
endif
ifeq (${OPENAIS_BUILD}, COVERAGE)
	CFLAGS += -O0 -g -ftest-coverage -fprofile-arcs
	LDFLAGS += -g -ftest-coverage -fprofile-arcs
	BUILD_DYNAMIC=0
endif

ifdef OPENAIS_PROFILE
	CFLAGS += -pg
	LDFLAGS += -pg
endif

ifeq (${LINK_WITH_RT}, 1)
	LDFLAGS += -lrt
endif

# platform specific CFLAGS, LDFLAGS
#
ifeq (${OPENAIS_COMPAT}, LINUX) 
	override CFLAGS += -DOPENAIS_LINUX
	override LDFLAGS += -ldl -lpthread
	override DYFLAGS += -rdynamic
endif
ifeq (${OPENAIS_COMPAT}, BSD)
	override CFLAGS += -DOPENAIS_BSD
	override LDFLAGS += -pthread
	override DYFLAGS += -export-dynamic
endif
ifeq (${OPENAIS_COMPAT}, DARWIN)
	override CFLAGS += -DOPENAIS_DARWIN
	override LDFLAGS += -lpthread
endif
