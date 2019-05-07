# Makefile
#
# Used to run the tests.

lit_tests := $(filter-out ./.%,$(shell find . -name "*.test"))
lit_testdirs := $(sort $(foreach t,$(lit_tests),$(dir $t)))
lit_targets := $(patsubst ./%/,check-%,$(lit_testdirs))

.PHONY: all clean clean-venv venv requirements
all: check-tools check-lit

include ../make-helpers/python_venv_requirements.mk

clean: clean-venv
	@echo "cleaning..."

.PHONY: check-tools check-lit $(lit_targets)
check-tools: venv requirements Makefile
	$(PYTHON_ROOT)pytest tools/

$(lit_targets) : check-%: venv requirements Makefile
	$(PYTHON_ROOT)lit $*/

check-lit: venv requirements Makefile
	$(PYTHON_ROOT)lit */
