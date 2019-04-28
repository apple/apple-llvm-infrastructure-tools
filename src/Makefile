# Makefile
#
# Used by bin/mt-helpers/build_executable.sh to build programs on-demand.
ifeq "$D" ""
$(error 'D' must be explicitly defined to an output directory for the programs)
endif

headers := $(wildcard *.h)
sources := $(wildcard *.cpp)
programs := $(patsubst %.cpp,$D/%,$(sources))

.PHONY: all clean
all: $(programs)

clean:
	rm -f $(programs)

$(programs): $D/%: %.cpp $(headers) Makefile
	clang -O2 -std=c++17 -lc++ -Wall -Wextra -o "$@" $<
