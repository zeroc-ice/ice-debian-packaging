RB_PREFIX=$(CURDIR)/$(DEB_DH_INSTALL_SOURCEDIR)/usr
RB_RBDIR = $(CURDIR)/$(DEB_DH_INSTALL_SOURCEDIR)$(shell ruby -e \
	'require "rbconfig"; puts RbConfig::expand("$$(vendordir)")')
RB_LIBDIR = $(CURDIR)/$(DEB_DH_INSTALL_SOURCEDIR)$(shell ruby -e \
	'require "rbconfig"; puts RbConfig::expand("$$(vendorarchdir)")')

debian/stamp-build-rb:: debian/stamp-build-cpp
	@if test ! -f $@ ; then \
		$(MAKE_INVOKE) RUBY=ruby -C rb all; \
	fi
	:> $@

debian/stamp-install-rb:: MAKE_ARGS := \
	RUBY=ruby \
	RUBY1.8=no \
	prefix=$(RB_PREFIX) \
	install_rubydir=$(RB_RBDIR) \
	install_libdir=$(RB_LIBDIR) \
	install_slicedir=$(RB_PREFIX)/share/Ice-$(V)/slice
debian/stamp-install-rb:: debian/stamp-build-rb
	-mkdir -p $(RB_RBDIR) $(RB_LIBDIR)
	@if test ! -f $@ ; then \
		$(MAKE_INVOKE) $(MAKE_ARGS) -C rb install ; \
	fi
	:> $@

clean-rb::
	$(MAKE_INVOKE) -C rb clean
