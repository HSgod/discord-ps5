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
#   make            build the PS5 application (needs the builder container)
#   make test       build and run the host tests (native toolchain, no container)
#   make ffpkg      build the PS5 application and its UFS2 image, copied to dist/
#   make stage      only stage the sources into the boilerplate harness
#   make clean      drop host build output
#   make distclean  also drop the staged tree inside the boilerplate

CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic

HOST_SOURCES := $(shell find core platform/host tests -name '*.cpp' | sort)
HOST_BINARY  := build/host-tests

# The boilerplate harness builds our code, because it is the only one of the two
# submodules that accepts outside sources (APP_SOURCE_DIR/APP_PARAM).
BOILERPLATE := third_party/ps5-native-app-boilerplate
TITLE_ID    := PPSA99070
STAGING     := $(BOILERPLATE)/.local/discord-ps5

APP_VARS := APP_SOURCE_DIR=.local/discord-ps5/src \
            APP_PARAM=.local/discord-ps5/sce_sys/param.json \
            APP_INCLUDE_PATHS=.local/discord-ps5/src

.PHONY: all app stage ffpkg test clean distclean

all: app

stage:
	@bash tools/stage-ps5-sources.sh

app: stage
	@printf '%s\n' '==> [app] PS5 build in the builder container'
	@scripts/dev.sh make -C $(BOILERPLATE) app $(APP_VARS)

ffpkg: stage
	@printf '%s\n' '==> [ffpkg] PS5 build and UFS2 image'
	@scripts/dev.sh make -C $(BOILERPLATE) ffpkg $(APP_VARS)
	@mkdir -p dist
	@cp -a $(BOILERPLATE)/dist/$(TITLE_ID).ffpkg dist/
	@printf '%s\n' "==> [ffpkg] dist/$(TITLE_ID).ffpkg"

$(HOST_BINARY): $(HOST_SOURCES)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I. $(HOST_SOURCES) -o $@

test: $(HOST_BINARY)
	@printf '%s\n' '==> [test] host unit tests'
	@./$(HOST_BINARY)

clean:
	@rm -rf build

distclean: clean
	@rm -rf $(STAGING)
