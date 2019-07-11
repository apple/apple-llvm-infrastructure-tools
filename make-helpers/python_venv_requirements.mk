makefile_dir := $(dir $(abspath $(firstword $(MAKEFILE_LIST))))
root_dir := $(shell dirname $(makefile_dir))

VENV_OUT=$(makefile_dir)/.python_env
REQUIREMENTS_OUT=$(VENV_OUT)/.requirements.txt.install_log
REQUIREMENTS_FILE=$(makefile_dir)/requirements.txt
GIT_APPLE_LLVM_INSTALL_OUT=$(VENV_OUT)/.git_apple_llvm.txt.install_log
SETUP_PY_FILE=$(root_dir)/setup.py

PYTHON_ROOT := $(VENV_OUT)/bin/

clean-venv:
	rm -rf $(REQUIREMENTS_OUT) $(VENV_OUT)

venv: $(VENV_OUT) Makefile

$(VENV_OUT):
	@echo "Setting up python venv..."
	python3 -m venv $(VENV_OUT)
	@echo ""

requirements: $(REQUIREMENTS_OUT) Makefile

$(REQUIREMENTS_OUT): $(REQUIREMENTS_FILE) | $(VENV_OUT)
	$(PYTHON_ROOT)pip install -r $(REQUIREMENTS_FILE) | tee $(REQUIREMENTS_OUT)

git_apple_llvm_package: $(GIT_APPLE_LLVM_INSTALL_OUT) $(SETUP_PY_FILE)

$(GIT_APPLE_LLVM_INSTALL_OUT): $(SETUP_PY_FILE) | $(VENV_OUT)
	$(PYTHON_ROOT)pip install -e $(root_dir) | tee $(GIT_APPLE_LLVM_INSTALL_OUT)
