debian/stamp-build-cpp::
	-mkdir -p cpp/bin cpp/lib
	@if test ! -f $@ ; then \
		$(MAKE_INVOKE) -C cpp all; \
		QT_HOME=/usr/include/qt4 $(MAKE_INVOKE) -C cpp/src/IceStorm/SqlDB all; \
		QT_HOME=/usr/include/qt4 $(MAKE_INVOKE) -C cpp/src/IceGrid/SqlDB all; \
		$(MAKE_INVOKE) -C cpp/doc all ; \
	fi
	:> $@

CPP_PREFIX=$(CURDIR)/$(DEB_DH_INSTALL_SOURCEDIR)/usr
debian/stamp-install-cpp:: MAKE_ARGS := \
	prefix=$(CPP_PREFIX) \
	install_docdir=$(CPP_PREFIX)/share/doc/ice$(R)-slice \
	install_slicedir=$(CPP_PREFIX)/share/Ice-$(V)/slice \
	install_libdir=$(CPP_PREFIX)/lib/$(DEB_HOST_MULTIARCH)
debian/stamp-install-cpp:: debian/stamp-build-cpp
	-mkdir -p $(CPP_PREFIX)/bin $(CPP_PREFIX)/lib/$(DEB_HOST_MULTIARCH)
	-mkdir -p $(CPP_PREFIX)/share/Ice-$(V)
	-mkdir -p $(CPP_PREFIX)/share/doc/ice$(R)-slice
	@if test ! -f $@ ; then \
		$(MAKE_INVOKE) $(MAKE_ARGS) -C cpp install ; \
		$(MAKE_INVOKE) $(MAKE_ARGS) -C cpp/src/IceStorm/SqlDB install; \
		$(MAKE_INVOKE) $(MAKE_ARGS) -C cpp/src/IceGrid/SqlDB install; \
		$(MAKE_INVOKE) $(MAKE_ARGS) -C cpp/doc install ; \
	fi
	:> $@

clean-cpp::
	$(MAKE_INVOKE) -C cpp clean
	$(MAKE_INVOKE) -C cpp/src/IceStorm/SqlDB clean
	$(MAKE_INVOKE) -C cpp/src/IceGrid/SqlDB clean
	$(MAKE_INVOKE) -C cpp/doc clean
