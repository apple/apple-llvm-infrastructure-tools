# Makefile
#
# Used to run the tests.

lit_tests := $(filter-out ./.%,$(shell find . -name "*.test" -o -name "*.py"))
lit_testdirs := $(sort $(foreach t,$(lit_tests),$(dir $t)))
lit_targets := $(patsubst ./%/,check-%,$(lit_testdirs))
testdir := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))


.PHONY: all clean clean-venv venv requirements
all: check-tools check-lit

include ../make-helpers/python_venv_requirements.mk

clean: clean-venv
	@echo "cleaning..."

.PHONY: check-lit $(lit_targets)

$(lit_targets) : check-%: venv requirements Makefile
	$(PYTHON_ROOT)lit $(testdir)/$*/

check-lit: venv requirements Makefile
	$(PYTHON_ROOT)lit $(testdir)/*/
