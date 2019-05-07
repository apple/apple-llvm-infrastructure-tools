# Makefile
#
# Used to run the tests.

.PHONY: all clean
all: check-tools check-lit-example

VENV_OUT=.python_env
REQUIREMENTS_OUT=$(VENV_OUT)/.requirements.txt.install_log
REQUIREMENTS_FILE=requirements.txt

PYTHON_ROOT := $(VENV_OUT)/bin/

clean:
	@echo "cleaning..."
	rm -rf $(REQUIREMENTS_OUT) $(VENV_OUT)

venv: $(VENV_OUT) Makefile

$(VENV_OUT):
	@echo "Setting up python venv..."
	python3 -m venv $(VENV_OUT)
	@echo ""

requirements: $(REQUIREMENTS_OUT) Makefile

$(REQUIREMENTS_OUT): $(REQUIREMENTS_FILE)
	$(PYTHON_ROOT)pip install -r $(REQUIREMENTS_FILE) | tee $(REQUIREMENTS_OUT)

check-tools: venv requirements Makefile
	$(PYTHON_ROOT)pytest tools/

check-lit-example: venv requirements Makefile
	$(PYTHON_ROOT)lit lit-example/
