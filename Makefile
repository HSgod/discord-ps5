# Discord PS5 - build entry point.
#
# Layout (research/discord-ps5/TASKS.md, T2.1):
#   core/           pure logic, no PS5 SDK -- compiles with the host toolchain
#   platform/ps5/   PS5 implementation of the platform seams
#   platform/host/  host substitutes, used by the tests only
#   ui/             screens
#   tests/          host tests
#   sce_sys/        title identity (param.json)
#
# Targets:
#   make test       build and run the host tests (native toolchain, no container)

CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic

HOST_SOURCES := $(shell find core platform/host tests -name '*.cpp' | sort)
HOST_BINARY  := build/host-tests

.PHONY: all test clean

all: test

$(HOST_BINARY): $(HOST_SOURCES)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I. $(HOST_SOURCES) -o $@

test: $(HOST_BINARY)
	@printf '%s\n' '==> [test] host unit tests'
	@./$(HOST_BINARY)

clean:
	@rm -rf build
