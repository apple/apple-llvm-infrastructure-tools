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

help:
	@echo "git apple-llvm can be installed using 'make install'"

install:
	@echo "Installing 'git-apple-llvm'"
	@echo "################################################################################"
	@echo "The following parameters are set:"
	@echo "  PIP = ${PIP}"
	@echo "  VERBOSE = ${VERBOSE}"
	@echo ""
	@echo "Installing package contents"
	@echo "################################################################################"
	$(PIP) install $(verbose_arg) .
	@echo ""
	@echo "################################################################################"
	@echo "installation succeeded: 'git apple-llvm' is now available!"

uninstall:
	@echo "Uninstalling 'git-apple-llvm'"
	$(PIP) uninstall $(verbose_arg) git_apple_llvm

.PHONY: help install uninstall
