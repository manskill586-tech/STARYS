CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1

test: test_core
	./test_core

trace: test_core
	./test_core -v

test_core: test_core.cpp ../src/swarm_core.h ../src/flywheel.h
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp

clean:
	rm -f test_core

.PHONY: test trace clean
