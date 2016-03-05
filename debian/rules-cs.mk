export MONO_SHARED_DIR=$(CURDIR)
CS_PREFIX=$(CURDIR)/$(DEB_DH_INSTALL_SOURCEDIR)/usr
CS_BINDIR=$(CS_PREFIX)/lib/cli/libzeroc-ice$(RV)

debian/stamp-build-cs:: MAKE_INVOKE := make \
	PKG_CONFIG_PATH=../../lib/pkgconfig:../lib/pkgconfig:lib/pkgconfig \
	UNAME=$(UNAME) MCS=/usr/bin/cli-csc $(NJOBS)
debian/stamp-build-cs:: debian/stamp-build-cpp
	-mkdir -p cs/bin cs/Assemblies
	-for i in src demo test ; do \
		find cs/$$i -type d \
			| sed -e 's.generated$$..g' -e 's.$$./generated.g' \
			| xargs mkdir -p ; \
	done
	@if test ! -f $@ ; then \
		$(MAKE_INVOKE) -C cs all ; \
	fi
	:> $@

debian/stamp-install-cs:: MAKE_INVOKE := $(DEB_MAKE_ENVVARS) make \
	NOGAC=true UNAME=$(UNAME) \
	prefix=$(CS_PREFIX) \
	install_clibindir=$(CS_BINDIR) \
	install_assembliesdir=$(CS_BINDIR) \
	install_slicedir=$(CS_PREFIX)/share/Ice-$(V)/slice $(NJOBS)
debian/stamp-install-cs:: debian/stamp-build-cs
	-mkdir -p $(CS_BINDIR) $(CS_PREFIX)/share/cli-common/packages.d
	-mkdir -p $(CS_PREFIX)/lib/pkgconfig $(CS_PREFIX)/bin
	@if test ! -f $@ ; then \
		$(MAKE_INVOKE) -C cs install ; \
	fi
	cp debian/iceboxnet.wrapper $(CS_PREFIX)/bin/iceboxnet-$(RV)
	cp -r debian/pkgconfig $(CS_PREFIX)/lib/$(DEB_HOST_MULTIARCH)
	chmod 755 $(CS_PREFIX)/bin/iceboxnet-$(RV)
	rm -f $(CS_BINDIR)/*.mdb
	chmod 644 $(CS_BINDIR)/*.dll
	:> $@


clean-cs::
	-$(MAKE) UNAME=$(UNAME) -C cs clean
