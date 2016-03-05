export CLASSPATH=/usr/share/java/db.jar
DESTDIR=$(CURDIR)/$(DEB_DH_INSTALL_SOURCEDIR)
PREFIX=/usr
JAVA_PREFIX=$(DESTDIR)$(PREFIX)

debian/stamp-build-java:: MAKE_INVOKE := make \
	DESTDIR=$(DESTDIR) \
	prefix=$(PREFIX) $(NJOBS)
debian/stamp-build-java:: debian/stamp-build-cpp
	@if test ! -f $@ ; then \
		LD_LIBRARY_PATH="$(CURDIR)/cpp/lib:$$LD_LIBRARY_PATH" ; \
		export LD_LIBRARY_PATH; $(MAKE_INVOKE) -C java all ; \
	fi
	:> $@

debian/stamp-install-java:: MAKE_INVOKE := make \
	DESTDIR=$(DESTDIR) \
	prefix=$(PREFIX) \
	install_slicedir=$(JAVA_PREFIX)/share/Ice-$(V)/slice $(NJOBS)
debian/stamp-install-java:: debian/stamp-build-java
	-mkdir -p $(JAVA_PREFIX)
	-mkdir -p $(JAVA_PREFIX)/share/java
	@if test ! -f $@ ; then \
	  DESTDIR=$(DESTDIR) prefix=$(PREFIX) \
	  $(MAKE_INVOKE) -C java install ; \
	  mv $(JAVA_PREFIX)/share/java/Ice.jar \
	    $(JAVA_PREFIX)/share/java/zeroc-ice-$(RV).jar ; \
	  ln -sf zeroc-ice-$(RV).jar \
	    $(JAVA_PREFIX)/share/java/Ice.jar ; \
	  mv $(JAVA_PREFIX)/share/java/Freeze.jar \
	    $(JAVA_PREFIX)/share/java/freeze-$(RV).jar ; \
	  ln -sf freeze-$(RV).jar \
	    $(JAVA_PREFIX)/share/java/Freeze.jar ; \
	  mv $(JAVA_PREFIX)/share/java/IceGridGUI.jar \
	     $(JAVA_PREFIX)/share/java/IceGridGUI-$(RV).jar ; \
	  cp debian/icegrid-gui.wrapper $(JAVA_PREFIX)/bin/icegrid-gui ; \
	  chmod 755 $(JAVA_PREFIX)/bin/icegrid-gui ;\
	fi
	:> $@

# make -C java clean may fail for binary-arch due to lack of ant
clean-java::
	-$(MAKE) -C java clean
