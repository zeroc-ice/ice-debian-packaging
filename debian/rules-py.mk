PY_PREFIX:=$(CURDIR)/$(DEB_DH_INSTALL_SOURCEDIR)/usr
DEFAULT_PYTHON_VERSION=$(shell pyversions -d)
PY_MODULES=IcePy
PYVERS:=$(shell pyversions -rv) $(shell py3versions -rv)
PYVERS:=$(PYVERS:python%=%)

debian/stamp-build-py:: $(PYVERS:%=debian/stamp-build-py-%)
	:> $@
debian/stamp-build-py-%: debian/stamp-build-cpp
	test -f $@ || for m in $(PY_MODULES) ; do \
	  mkdir -p py/modules/$$m-$* ; \
	  ( cd py/modules/$$m-$* && \
	    ln -sf ../$$m/*.h ../$$m/*.cpp ../$$m/Makefile ../$$m/.depend . ) ; \
	  $(MAKE_INVOKE) PYTHON_FLAGS="$(shell python$*-config --includes)"  \
			 PYTHON_LIBS="$(shell python$*-config --libs)"  \
		 	 PYTHON_VERSION=python$* -C py/modules/$$m-$* all; \
	done
	:> $@

debian/stamp-install-py:: debian/stamp-build-py $(PYVERS:%=debian/stamp-install-py-%)
	:> $@
debian/stamp-install-py-%:: MAKE_ARGS = \
	PYTHON_LIBS="" \
	install_slicedir=$(PY_PREFIX)/share/Ice-$(V)/slice \
	prefix=$(PY_PREFIX) \
	libdir=. \
	PYTHON_VERSION=python$* \
	install_pythondir=$(PY_PREFIX)/lib/python$*/dist-packages \
	install_pylibdir=$(PY_PREFIX)/lib/python$*/dist-packages
debian/stamp-install-py-%:: debian/stamp-build-py-%
	mkdir -p $(PY_PREFIX)/share/Ice-$(V)/slice
	mkdir -p $(PY_PREFIX)/lib/python$*/dist-packages
	$(MAKE_INVOKE) $(MAKE_ARGS) -C py/python install
	test -f $@ || for m in $(PY_MODULES) ; do \
	  $(MAKE_INVOKE) $(MAKE_ARGS) -C py/modules/IcePy-$* install; \
	done
	: # dh_python3 can't handle symlinks to extensions very well
	find $(PY_PREFIX)/lib/python$*/dist-packages -name 'IcePy.so.*' -type l | xargs -r rm -f
	mv $(PY_PREFIX)/lib/python$*/dist-packages/IcePy.so.* $(PY_PREFIX)/lib/python$*/dist-packages/IcePy.so
	:> $@

override_dh_python3:
	dh_python3 -p python3-zeroc-ice

clean-py:
	$(MAKE_INVOKE) -C py clean
	  for m in $(PY_MODULES) ; do \
	    $(RM) -rf py/modules/$$m-*; \
          done;
