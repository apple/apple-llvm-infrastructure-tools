# Programs.mk
ifeq "$D" ""
$(error 'D' must be explicitly defined to an output directory for the programs)
endif

headers := $(wildcard $S*.h)
sources := $(wildcard $S*.cpp)
programs := $(patsubst $S%.cpp,$D/%,$(sources))

$(programs): $D/%: $S%.cpp $(headers) $SPrograms.mk
	mkdir -p "$(@D)" && clang++ -O2 -std=c++17 -fno-exceptions -fno-rtti -lc++ -Wall -Wextra -o "$@" $<

.PHONY: programs clean-programs
programs: $(programs) ;
clean-programs:
	rm -f $(programs)
