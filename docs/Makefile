# Minimal makefile for Sphinx documentation
#

# Put it first so that "make" without argument is like "make help".
help: venv requirements Makefile
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

include ../make-helpers/python_venv_requirements.make

# You can set these variables from the command line.
SPHINXOPTS    =
SPHINXBUILD   = $(PYTHON_ROOT)sphinx-build
SOURCEDIR     = .
BUILDDIR      = _build

.PHONY: help Makefile

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
html: venv requirements Makefile
	$(SPHINXBUILD) -M html "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

clean: clean-venv
	@echo "cleaning up"
	rm -rf $(BUILDDIR)
