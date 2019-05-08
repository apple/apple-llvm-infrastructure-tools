# Makefile
#
# Used to run the tests.

lit_tests := $(filter-out ./.%,$(shell find . -name "*.test" -o -name "*.py"))
lit_testdirs := $(sort $(foreach t,$(lit_tests),$(dir $t)))
lit_targets := $(patsubst ./%/,check-%,$(lit_testdirs))
testdir := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))


.PHONY: all clean clean-venv venv requirements
all: check-tools check-lit

S := $(testdir)../src/
D := $(testdir)Built

include ../make-helpers/python_venv_requirements.mk
include ../src/Programs.mk

clean: clean-venv clean-programs clean-lit
	@echo "cleaning..."

.PHONY: clean-lit check-lit $(lit_targets)
clean-lit:
	rm -rf Run.lit

$(lit_targets) : check-%: venv requirements Makefile
	$(PYTHON_ROOT)lit -sv $(testdir)/$*/

check-lit: venv requirements Makefile
	$(PYTHON_ROOT)lit -sv $(testdir)/*/

check-lit check-split2mono check-svn2git: programs
