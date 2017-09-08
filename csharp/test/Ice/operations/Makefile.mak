# **********************************************************************
#
# Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
#
# This copy of Ice is licensed to you under the terms described in the
# ICE_LICENSE file included in this distribution.
#
# **********************************************************************

top_srcdir	= ..\..\..

TARGETS		= client.exe server.exe collocated.exe serveramd.exe servertie.exe serveramdtie.exe

C_SRCS		= AllTests.cs Client.cs Twoways.cs TwowaysAMI.cs BatchOneways.cs Oneways.cs \
		  BatchOnewaysAMI.cs OnewaysAMI.cs ..\..\TestCommon\TestApp.cs
S_SRCS		= MyDerivedClassI.cs Server.cs
COL_SRCS	= AllTests.cs Collocated.cs MyDerivedClassI.cs Twoways.cs TwowaysAMI.cs \
		  BatchOneways.cs BatchOnewaysAMI.cs Oneways.cs OnewaysAMI.cs \
		  ..\..\TestCommon\TestApp.cs
SAMD_SRCS	= MyDerivedClassAMDI.cs Server.cs
STIE_SRCS	= MyDerivedClassTieI.cs Server.cs
SAMD_TIE_SRCS	= MyDerivedClassAMDTieI.cs Server.cs

GEN_SRCS	= $(GDIR)\Test.cs
GEN_AMD_SRCS	= $(GDIR)\TestAMD.cs

SDIR		= .

GDIR		= generated

!include $(top_srcdir)\config\Make.rules.mak.cs

MCSFLAGS	= $(MCSFLAGS) -target:exe

SLICE2CSFLAGS	= $(SLICE2CSFLAGS) --tie -I. -I"$(slicedir)"

client.exe: $(C_SRCS) $(GEN_SRCS)
	$(MCS) $(MCSFLAGS) -out:$@ -r:"$(refdir)\Ice.dll" $(C_SRCS) $(GEN_SRCS)

server.exe: $(S_SRCS) $(GEN_SRCS)
	$(MCS) $(MCSFLAGS) -out:$@ -r:"$(refdir)\Ice.dll" $(S_SRCS) $(GEN_SRCS)

collocated.exe: $(COL_SRCS) $(GEN_SRCS)
	$(MCS) $(MCSFLAGS) -out:$@ -r:"$(refdir)\Ice.dll" $(COL_SRCS) $(GEN_SRCS)

serveramd.exe: $(SAMD_SRCS) $(GEN_AMD_SRCS)
	$(MCS) $(MCSFLAGS) -out:$@ -r:"$(refdir)\Ice.dll" $(SAMD_SRCS) $(GEN_AMD_SRCS)

servertie.exe: $(STIE_SRCS) $(GEN_SRCS)
	$(MCS) $(MCSFLAGS) -out:$@ -r:"$(refdir)\Ice.dll" $(STIE_SRCS) $(GEN_SRCS)

serveramdtie.exe: $(SAMD_TIE_SRCS) $(GEN_AMD_SRCS)
	$(MCS) $(MCSFLAGS) -out:$@ -r:"$(refdir)\Ice.dll" $(SAMD_TIE_SRCS) $(GEN_AMD_SRCS)
