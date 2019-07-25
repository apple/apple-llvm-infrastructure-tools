# Makefile
#
# Used to run the tests.

this_makefile := $(lastword $(MAKEFILE_LIST))
testdir := $(dir $(abspath $(this_makefile)))

lit_tests := $(filter-out ./.%,$(shell find . -name "*.test"))
lit_test_file_targets := $(sort $(patsubst ./%.test,check-%, \
														 $(filter %.test,$(lit_tests))))
lit_testdirs := $(filter-out ./,$(sort \
	$(shell printf "%s\n" $(sort $(foreach t,$(lit_tests),$(dir $t))) \
	| awk -f $(testdir)/bin/extract-targets-from-test-directories.awk)))
lit_targets := $(patsubst ./%/,check-%,$(lit_testdirs))

pytest_dirs := auto-updater git-tools config pr
pytest_targets := $(foreach t,$(pytest_dirs),check-$t)

.PHONY: all clean clean-venv venv requirements
all: check-all ;

S := $(testdir)../src/
D := $(testdir)Built

include ../make-helpers/python_venv_requirements.mk
include ../src/Programs.mk

clean: clean-venv clean-programs clean-lit
	@echo "cleaning..."

.PHONY: clean-lit help $(lit_targets) $(pytest_targets) check-all
clean-lit:
	rm -rf Run.lit

check-all: check-lit check-lint check-types $(pytest_targets)

$(lit_targets) : check-%: venv requirements Makefile
	$(PYTHON_ROOT)lit -sv $(testdir)/$*/

$(lit_test_file_targets) : check-%: venv requirements Makefile
	$(PYTHON_ROOT)lit -sv $(testdir)/$*.test

$(pytest_targets) : check-%: venv git_apple_llvm_package requirements Makefile
	$(PYTHON_ROOT)pytest --log-level=debug $(testdir)/$*/

check-lit: venv requirements Makefile
	$(PYTHON_ROOT)lit -sv $(testdir)/*/

python_sources := $(shell find $(testdir) $(testdir)../git_apple_llvm $(testdir)../src -not \( -path $(testdir)/.python_env -prune \) -not \( -path $(testdir)/Run.lit -prune \) -name "*.py")

check-lint: venv git_apple_llvm_package requirements Makefile
	$(PYTHON_ROOT)flake8 --exclude="*env" --tee flake8_report.pep8.txt $(python_sources)

check-types: venv git_apple_llvm_package requirements Makefile
	env MYPYPATH="$(S)" $(PYTHON_ROOT)mypy $(python_sources) --ignore-missing-imports

$(filter check-split2mono/%,$(lit_targets) $(lit_test_file_targets)) \
$(filter check-svn2git/%,$(lit_targets) $(lit_test_file_targets)) \
	check-split2mono check-svn2git: programs

help:
	@echo "Supported check targets:"
	@printf "    %s\n" \
		$(sort $(lit_targets) $(pytest_targets) $(lit_targets) \
		$(shell awk '$$1 ~ /^check-[0-9a-z-]+:$$/ {print substr($$1,1,length($$1)-1)}' \
		$(this_makefile)))

check-lit check-split2mono check-svn2git \
	$(filter check-mt%,$(lit_targets) $(lit_test_file_targets)): programs
