# **********************************************************************
#
# Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
#
# This copy of Ice is licensed to you under the terms described in the
# ICE_LICENSE file included in this distribution.
#
# **********************************************************************

top_srcdir	= ..

!include $(top_srcdir)\config\Make.rules.mak.cs

SUBDIRS		= Slice \
		  IceUtil \
		  Ice \
		  IceBox \
		  Glacier2 \
		  IceGrid \
		  IceDiscovery

!if "$(UNITY)" != "yes"
SUBDIRS		= $(SUBDIRS) \
		  IceSSL
!endif

$(EVERYTHING)::
	@for %i in ( $(SUBDIRS) ) do \
	    @echo "making $@ in %i" && \
	    cmd /c "cd %i && $(MAKE) -nologo -f Makefile.mak $@" || exit 1

test::
	@python $(top_srcdir)/allTests.py
