PHP_LIBDIR=$(CURDIR)/$(DEB_DH_INSTALL_SOURCEDIR)$(shell php-config5 --extension-dir)
PHP_API=$(shell php-config5 --phpapi)

debian/stamp-build-php:: debian/stamp-build-cpp
	-mkdir -p php/lib
	@if test ! -f $@ ; then \
		$(MAKE_INVOKE) -C php all; \
	fi
	:> $@

debian/stamp-install-php:: MAKE_ARGS := \
	prefix=$(CURDIR)/$(DEB_DH_INSTALL_SOURCEDIR)/usr \
	install_phplibdir=$(PHP_LIBDIR) \
	install_slicedir=$(CURDIR)/$(DEB_DH_INSTALL_SOURCEDIR)/usr/share/Ice-$(V)/slice
debian/stamp-install-php:: debian/stamp-build-php
	-mkdir -p $(PHP_LIBDIR) $(DEB_DH_INSTALL_SOURCEDIR)/etc/php5/conf.d
	-mkdir -p $(CURDIR)/$(DEB_DH_INSTALL_SOURCEDIR)/usr/share/Ice-$(V)/slice
	@if test ! -f $@ ; then \
		$(MAKE_INVOKE) $(MAKE_ARGS) -C php install ; \
	fi
	-cp debian/IcePHP.ini $(DEB_DH_INSTALL_SOURCEDIR)/etc/php5/conf.d
	echo "php:Depends=phpapi-$(PHP_API)" >>  debian/php-zeroc-ice.substvars
	sed 's/@PHP_API@/$(PHP_API)/' debian/php-zeroc-ice.install.in > debian/php-zeroc-ice.install
	:> $@

clean-php::
	$(MAKE_INVOKE) -C php clean
	rm -f debian/php-zeroc-ice.install
