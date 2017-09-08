# **********************************************************************
#
# Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
#
# This copy of Ice is licensed to you under the terms described in the
# ICE_LICENSE file included in this distribution.
#
# **********************************************************************

top_srcdir	= ..\..

NAME		= $(top_srcdir)\bin\slice2js.exe

TARGETS		= $(NAME)

OBJS		= .\Gen.obj \
		  .\JsUtil.obj \
		  .\Main.obj

!include $(top_srcdir)/config/Make.rules.mak

CPPFLAGS	= -I. -I.. $(CPPFLAGS) -DWIN32_LEAN_AND_MEAN

!if "$(GENERATE_PDB)" == "yes"
PDBFLAGS        = /pdb:$(NAME:.exe=.pdb)
!endif

RES_FILE        = Slice2Js.res

$(NAME): $(OBJS) Slice2Js.res
	$(LINK) $(LD_EXEFLAGS) $(PDBFLAGS) $(OBJS) $(SETARGV) $(PREOUT)$@ $(SLICE_LIBS) 	 \
		$(BASELIBS) $(RES_FILE)
	@if exist $@.manifest echo ^ ^ ^ Embedding manifest using $(MT) && \
	    $(MT) -nologo -manifest $@.manifest -outputresource:$@;#1 && del /q $@.manifest
	@if defined SIGN_CERTIFICATE echo ^ ^ ^ Signing $@ && \
		signtool sign /f "$(SIGN_CERTIFICATE)" /p $(SIGN_PASSWORD) /t $(SIGN_TIMESTAMPSERVER) $@

clean::
	del /q $(NAME:.exe=.*)
	del /q Slice2Js.res

install:: all
	copy $(NAME) "$(install_bindir)"

!if "$(GENERATE_PDB)" == "yes"

install:: all
	copy $(NAME:.exe=.pdb) "$(install_bindir)"

!endif
