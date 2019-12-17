################################################################################
# Configuration variables.
################################################################################

VERBOSE = 0

################################################################################

ifeq "$(VERBOSE)" "1"
  verbose_arg='-v'
else
  verbose_arg=
endif

ifeq ($(PREFIX),)
  PREFIX := /usr/local
  # Blank out VIRTUALENV and use the system pip
  VIRTUALENV :=
  PIP := pip3
else
  VIRTUALENV := $(PREFIX)
  PIP := $(PREFIX)/bin/pip3
endif

INSTALL := install

help:
	@echo "git apple-llvm can be installed using 'make install'"

install: show-params install-python-package install-git-scripts

show-params:
	@echo "'git-apple-llvm' version:"
	@echo "  ${shell utils/get-git-revision.sh}"
	@echo "################################################################################"
	@echo "The following parameters are set:"
	@echo "  PIP = ${PIP}"
	@echo "  VERBOSE = ${VERBOSE}"
	@echo "  PREFIX = ${PREFIX}"
	@echo "  DESTDIR = ${DESTDIR}"
	@echo ""

install-python-package: $(VIRTUALENV)
	@echo "Installing python packages"
	@echo "################################################################################"
	env GIT_APPLE_LLVM_NO_BIN_LIBEXEC=1 $(PIP) install $(verbose_arg) .
	@echo ""

$(VIRTUALENV):
	@echo "Creating virtualenv at $(PREFIX) "
	@echo "################################################################################"
	python3 -m venv $(PREFIX)
	@echo ""

LIBEXEC_DIR := $(DESTDIR)$(PREFIX)/libexec/apple-llvm

install-git-scripts: svn2git split2mono
	@echo "Installing 'git apple-llvm' bash scripts"
	@echo "################################################################################"
	$(INSTALL) bin/git-apple-llvm $(DESTDIR)$(PREFIX)/bin/
	$(INSTALL) -d $(LIBEXEC_DIR)
	tar cf - libexec/apple-llvm | (cd $(DESTDIR)$(PREFIX); tar xf -)
	echo "${shell utils/get-git-revision.sh}" > $(LIBEXEC_DIR)/helpers/version
	@echo ""
	@echo "################################################################################"
	@echo "Installation succeeded: 'git apple-llvm' is now available!"

svn2git split2mono:
	make -C src D="$(LIBEXEC_DIR)" $(LIBEXEC_DIR)/$@

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
