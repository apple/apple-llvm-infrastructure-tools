# Makefile
#
# Used to run the tests.

.PHONY: all clean clean-venv venv requirements
all: check-tools check-lit-example

include ../make-helpers/python_venv_requirements.make

clean: clean-venv
	@echo "cleaning..."

check-tools: venv requirements Makefile
	$(PYTHON_ROOT)pytest tools/

check-lit-example: venv requirements Makefile
	$(PYTHON_ROOT)lit lit-example/
