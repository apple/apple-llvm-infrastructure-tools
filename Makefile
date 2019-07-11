################################################################################
# Configuration variables.
################################################################################

PIP = pip3
VERBOSE = 0

################################################################################

ifeq "$(VERBOSE)" "1"
	verbose_arg='-v'
else
	verbose_arg=
endif

ifeq ($(PREFIX),)
	PREFIX := /usr/local
endif

INSTALL := install

help:
	@echo "git apple-llvm can be installed using 'make install'"

install:
	@echo "Installing 'git-apple-llvm'"
	@echo "  ${shell utils/get-git-revision.sh}"
	@echo "################################################################################"
	@echo "The following parameters are set:"
	@echo "  PIP = ${PIP}"
	@echo "  VERBOSE = ${VERBOSE}"
	@echo "  PREFIX = ${PREFIX}"
	@echo "  DESTDIR = ${DESTDIR}"
	@echo ""
	@echo "Installing python packages"
	@echo "################################################################################"
	$(PIP) install $(verbose_arg) .
	@echo ""
	@echo "Installing 'git apple-llvm' bash scripts"
	@echo "################################################################################"
	$(INSTALL) bin/git-apple-llvm $(DESTDIR)$(PREFIX)/bin/
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/libexec/apple-llvm
	tar cf - libexec/apple-llvm | (cd $(DESTDIR)$(PREFIX); tar xf -)
	echo "${shell utils/get-git-revision.sh}" > $(DESTDIR)$(PREFIX)/libexec/apple-llvm/helpers/version
	@echo ""
	@echo "################################################################################"
	@echo "installation succeeded: 'git apple-llvm' is now available!"

uninstall:
	@echo "Uninstalling 'git-apple-llvm'"
	@echo "################################################################################"
	@echo ""
	@echo "Uninstalling python packages"
	@echo "################################################################################"
	$(PIP) uninstall -y $(verbose_arg) git_apple_llvm
	@echo ""
	@echo "Uninstalling 'git apple-llvm' bash scripts"
	@echo "################################################################################"
	rm -rf $(DESTDIR)$(PREFIX)/bin/git-apple-llvm
	rm -rf $(DESTDIR)$(PREFIX)/libexec/apple-llvm
	@echo ""
	@echo "################################################################################"
	@echo "'git apple-llvm' was successfully uninstalled!"

.PHONY: help install uninstall
