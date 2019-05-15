# Makefile
#
# Used to run the tests.

lit_tests := $(filter-out ./.%,$(shell find . -name "*.test"))
lit_test_targets := $(sort $(patsubst ./%.test,check-%, \
														 $(filter %.test,$(lit_tests))))
lit_testdirs := $(filter-out ./,$(sort \
	$(shell printf "%s\n" $(sort $(foreach t,$(lit_tests),$(dir $t))) \
	| awk '{ \
  print $$1; \
	i = match($$1, /[^\/]+\/$$/); \
	while (i) { \
		$$1 = substr($$1, 1, i-1); \
		print $$1; \
	  i = match($$1, /[^\/]+\/$$/); \
	}}')))
lit_targets := $(patsubst ./%/,check-%,$(lit_testdirs))
this_makefile := $(lastword $(MAKEFILE_LIST))
testdir := $(dir $(abspath $(this_makefile)))

pytest_dirs := auto-updater
pytest_targets := $(foreach t,$(pytest_dirs),check-$t)

.PHONY: all clean clean-venv venv requirements
all: $(lit_targets) $(pytest_targets) check-lint check-types ;

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

$(lit_test_targets) : check-%: venv requirements Makefile
	$(PYTHON_ROOT)lit -sv $(testdir)/$*.test

$(pytest_targets) : check-%: venv requirements Makefile
	$(PYTHON_ROOT)pytest $(testdir)/$*/

check-lit: venv requirements Makefile
	$(PYTHON_ROOT)lit -sv $(testdir)/*/

python_sources := $(shell find $(testdir) $(testdir)../libexec $(testdir)../src -not \( -path $(testdir)/.python_env -prune \) -name "*.py")

check-lint: venv requirements Makefile
	$(PYTHON_ROOT)flake8 --exclude="*env" --tee flake8_report.pep8.txt $(python_sources)

check-types: venv requirements Makefile
	env MYPYPATH="$(S):$(testdir)../libexec/apple-llvm/src" $(PYTHON_ROOT)mypy $(python_sources)

check-split2mono check-svn2git: programs

help:
	@echo "Supported check targets:"
	@printf "    %s\n" \
		$(sort $(lit_targets) $(pytest_targets) $(lit_targets) \
		$(shell awk '$$1 ~ /^check-[0-9a-z-]+:$$/ {print substr($$1,1,length($$1)-1)}' \
		$(this_makefile)))

check-lit check-split2mono check-svn2git \
	$(filter check-mt%,$(lit_targets) $(lit_test_targets)): programs
