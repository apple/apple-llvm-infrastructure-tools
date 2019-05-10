# Makefile
#
# Used to run the tests.

lit_tests := $(filter-out ./.%,$(shell find . -name "*.test"))
lit_testdirs := $(sort $(foreach t,$(lit_tests),$(dir $t)))
lit_targets := $(patsubst ./%/,check-%,$(lit_testdirs))
testdir := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

pytest_dirs := auto-updater
pytest_targets := $(foreach t,$(pytest_dirs),check-$t)

.PHONY: all clean clean-venv venv requirements
all: $(lit_targets) $(pytest_targets) check-lint check-types

S := $(testdir)../src/
D := $(testdir)Built

include ../make-helpers/python_venv_requirements.mk
include ../src/Programs.mk

clean: clean-venv clean-programs clean-lit
	@echo "cleaning..."

.PHONY: clean-lit help $(lit_targets) $(pytest_targets)
clean-lit:
	rm -rf Run.lit

$(lit_targets) : check-%: venv requirements Makefile
	$(PYTHON_ROOT)lit -sv $(testdir)/$*/

$(pytest_targets) : check-%: venv requirements Makefile
	$(PYTHON_ROOT)pytest $(testdir)/$*/

check-lint : venv requirements Makefile
	$(PYTHON_ROOT)flake8 --exclude="*env" --tee flake8_report.pep8.txt $(S) $(testdir)

check-types : venv requirements Makefile
	env MYPYPATH=$(S) $(PYTHON_ROOT)mypy $(S)**/*.py $(testdir)/**/*.py

check-split2mono check-svn2git: programs

help:
	@echo "Supported check targets:"
	@echo $(lit_targets) $(pytest_targets)
