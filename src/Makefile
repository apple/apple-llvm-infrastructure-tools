# Makefile
#
# Used by bin/mt-helpers/build_executable.sh to build programs on-demand.
ifeq "$D" ""
$(error 'D' must be explicitly defined to an output directory for the programs)
endif

headers := $(wildcard *.h)
sources := $(wildcard *.cpp)
programs := $(patsubst %.cpp,$D/%,$(sources))
keep := $D/.keep

.PHONY: all clean
all:: $(programs) ;

clean:
	rm -f $(programs) $(keep)

$(keep): Makefile
	mkdir -p "$(@D)"
	touch "$@"

$(programs): $D/%: %.cpp $(headers) $(keep)
	clang -O2 -std=c++17 -lc++ -o "$@" $<
