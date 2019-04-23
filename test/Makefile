# Makefile
#
# Used to run the tests.

.PHONY: all clean
all: check-tools

REQUIREMENTS_OUT=.requirements.txt.install_log
REQUIREMENTS_FILE=requirements.txt

clean:
	@echo "cleaning..."
	rm -rf $(REQUIREMENTS_OUT)

requirements: $(REQUIREMENTS_OUT)

$(REQUIREMENTS_OUT): $(REQUIREMENTS_FILE)
	python3 -m pip install --user -r $(REQUIREMENTS_FILE) | tee $(REQUIREMENTS_OUT)

check-tools: requirements Makefile
	python3 -m pytest tools/
