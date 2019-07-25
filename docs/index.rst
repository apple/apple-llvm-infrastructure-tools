.. apple-llvm-infrastructure-tools documentation master file, created by
   sphinx-quickstart on Fri Apr 19 17:56:16 2019.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

An overview of infrastructure and tools for Apple fork of llvm-project
======================================================================

This is a collection of tools for maintaining LLVM-project related
infrastructure, including CI, automerging, monorepo transition, and others.

The documentation is broken down into several high-level groupings targeted at different audiences:

User Guides
===========

Are you an engineer working on llvm-project?
These guides are there to let you know how to use `git apple-llvm` to help your work on the llvm-project monorepo!

.. toctree::
   :maxdepth: 2

   monorepo-transition
   Getting started with git apple-llvm <git-apple-llvm>
   working-on-github-apple-llvm-project

Tools Documentation
===================

The documents in this section are the reference manuals for the
invidual tools provided by `git apple-llvm`.

Tools for engineers
*******************

The tools listed below simplify some of the day-to-day tasks for
the engineers who are working with the llvm-project monorepo:

.. toctree::
   :maxdepth: 1

   git-apple-llvm: pr: Tool for creating, testing and merging pull requests <git-apple-llvm-pr>

Monorepo transition tools for end users
***************************************

The tools listed below are useful during the monorepo transition phase, when
the monorepo is still not the canonical repository, and the split repositories
are the source of truth:

.. toctree::
   :maxdepth: 1

   git-apple-llvm: push: Tool for pushing commits to split repositories <git-apple-llvm-push>

Monorepo branch maintenance tools
*********************************

The tools listed below are used to continously maintain the downstream monorepo hierarchy,
either by forwarding branches, or by merging upstream branches downstream:

.. toctree::
   :maxdepth: 1

   git-apple-llvm: fwd: Tool for forwarding branches between two remotes <git-apple-llvm-fwd>

Monorepo generation tools
*************************

Downstream monorepo branches can be generated using the `git apple-llvm mt` tool:

.. toctree::
   :maxdepth: 1

   git-apple-llvm-mt
   Configuring `git apple-llvm mt` using mt-config files <mt-config>

Other tools
***********

.. toctree::
   :maxdepth: 1

   git-apple-llvm-count-merged

Development Process Documentation
=================================

Are you interested in contributing to the infrastructure and tools?
This section has the information that will let you know how you can get started!

.. toctree::
   :maxdepth: 2

   ContributingToTools
