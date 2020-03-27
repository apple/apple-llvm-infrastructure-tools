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

# Note: PYTEST is lazy because PYTHON_ROOT is set dynamically.
PYTEST = $(PYTHON_ROOT)pytest
PYTESTFLAGS := -n auto --log-level=debug
pytest_dirs := auto-updater git-tools config ci pr am
pytest_targets := $(foreach t,$(pytest_dirs),check-$t)

.PHONY: all help clean clean-venv venv requirements
all: check-all ;

S := $(testdir)../src/
D := $(testdir)Built

include ../make-helpers/python_venv_requirements.mk
include ../src/Programs.mk

clean: clean-venv clean-programs clean-lit
	@echo "cleaning..."

CHECK_ALL_TARGETS := check-lit check-lint check-types check-pytest
.PHONY: $(lit_targets) $(pytest_targets)
.PHONY: check-all $(CHECK_ALL_TARGETS)

.PHONY: clean-lit
clean-lit:
	rm -rf Run.lit

check-all: $(CHECK_ALL_TARGETS)

$(lit_targets) : check-%: venv requirements Makefile
	$(PYTHON_ROOT)lit -sv $(testdir)/$*/

$(lit_test_file_targets) : check-%: venv requirements Makefile
	$(PYTHON_ROOT)lit -sv $(testdir)/$*.test

$(pytest_targets) : check-%: venv git_apple_llvm_package requirements Makefile
	@printf "\n***********************************************************\n"
	@printf "%s: check pytests for %s\n" "$@" "$*"
	$(PYTEST) $(PYTESTFLAGS) $(testdir)/$*/

check-pytest: venv git_apple_llvm_package requirements Makefile
	@printf "\n***********************************************************\n"
	@printf "%s: check all pytests\n" "$@"
	@printf " => includes %s\n" $(pytest_targets)
	$(PYTEST) $(PYTESTFLAGS) $(addprefix $(testdir)/,$(pytest_dirs))

check-lit: venv requirements Makefile
	@printf "\n***********************************************************\n"
	@printf "%s: check lit tests\n" "$@"
	$(PYTHON_ROOT)lit -sv $(testdir)/*/

python_sources := $(shell find $(testdir) $(testdir)../git_apple_llvm $(testdir)../src -not \( -path $(testdir)/.python_env -prune \) -not \( -path $(testdir)/Run.lit -prune \) -name "*.py")

check-lint: venv git_apple_llvm_package requirements Makefile
	@printf "\n***********************************************************\n"
	@printf "%s: check for python linting errors\n" "$@"
	$(PYTHON_ROOT)flake8 --exclude="*env" --tee flake8_report.pep8.txt $(python_sources)

check-types: venv git_apple_llvm_package requirements Makefile
	@printf "\n***********************************************************\n"
	@printf "%s: check for python type errors\n" "$@"
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

# List all the check-* that should be ordered somehow.  This should probably be
# the full list, aside from check-all, to avoid overlapping output.
ALL_ORDERED_CHECKS := $(CHECK_ALL_TARGETS) $(pytest_targets) \
	$(lit_targets) $(lit_test_file_targets) \
	check-split2mono check-svn2git

# The subset of ALL_ORDERED_CHECKS that are "active", based on MAKECMDGOALS.
# - If MAKECMDGOALS is empty, that triggers all of them.
# - If MAKECMDGOALS has all or check-all, that triggers all of them.
# - Otherwise, just pull the MAKECMDGOALS that are listed directly.
ACTIVE_ORDERED_CHECKS := \
	$(if $(MAKECMDGOALS),\
		$(if $(filter all check-all,$(MAKECMDGOALS)),\
			$(CHECK_ALL_TARGETS),\
			$(filter $(ALL_ORDERED_CHECKS),$(MAKECMDGOALS))),\
		$(CHECK_ALL_TARGETS))

# $(eval $(call order_checks,check))
#
# Iff 'check' is in ACTIVE_ORDERED_CHECKS, removes it/them, and causes
# everything that remains to depend on it/them.
#
# Can be called repeatedly to set a priority order for running checks.
define order_checks
ifneq ("$$(filter $(1),$$(ACTIVE_ORDERED_CHECKS))","")
NEXT_ACTIVE_ORDERED_CHECKS := $$(filter-out $(1),$$(ACTIVE_ORDERED_CHECKS))
$$(NEXT_ACTIVE_ORDERED_CHECKS) : | $$(filter $(1),$$(ACTIVE_ORDERED_CHECKS))
ACTIVE_ORDERED_CHECKS := $$(NEXT_ACTIVE_ORDERED_CHECKS)
endif
endef

# Run check-types first, then check-lint, then all the pytests, and then
# anything else (usually check-lit).
$(eval $(call order_checks,check-types))
$(eval $(call order_checks,check-lint))
$(eval $(call order_checks,$(pytest_targets) check-pytest))
