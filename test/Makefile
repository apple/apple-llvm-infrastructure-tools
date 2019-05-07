# Makefile
#
# Used to run the tests.

lit_configs := $(shell ls */lit.cfg.py)
lit_targets := $(patsubst %/lit.cfg.py,check-%,$(lit_configs))

.PHONY: all clean clean-venv venv requirements
all: check-tools $(lit_targets)

include ../make-helpers/python_venv_requirements.mk

clean: clean-venv
	@echo "cleaning..."

check-tools: venv requirements Makefile
	$(PYTHON_ROOT)pytest tools/

$(lit_targets) : check-%: venv requirements Makefile %/lit.cfg.py
	$(PYTHON_ROOT)lit $*/
