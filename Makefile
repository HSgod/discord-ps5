# Discord PS5 - build entry point.
#
# Layout:
#   core/           pure logic, no PS5 SDK -- compiles with the host toolchain
#   platform/ps5/   PS5 implementation of the platform seams
#   platform/host/  host substitutes: the test sinks and the snapshot runner
#   ui/             screens, and the vendored drawing kit under ui/kit/
#   tests/          host tests
#   sce_sys/        title identity (param.json)
#
# Targets:
#   make            build the PS5 application (needs the builder container)
#   make test       build and run the host tests (native toolchain, no container)
#   make host-snapshots  render every screen to build/snapshots/*.png (container, Mesa)
#   make ffpkg      build the PS5 application and its UFS2 image, copied to dist/
#   make stage      only stage the sources into the boilerplate harness
#   make clean      drop host build output
#   make distclean  also drop the staged tree inside the boilerplate

CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic

# snapshot_main.cpp has a main() of its own: it is the snapshot runner, not a test.
HOST_SOURCES := $(shell find core platform/host tests -name '*.cpp' ! -name 'snapshot_main.cpp' | sort)
HOST_BINARY  := build/host-tests

# Which of the kit's themes the snapshots are rendered in.
THEME ?= acrylic

# The boilerplate harness builds our code, because it is the only one of the two
# submodules that accepts outside sources (APP_SOURCE_DIR/APP_PARAM).
BOILERPLATE := third_party/ps5-native-app-boilerplate
TITLE_ID    := PPSA99070
STAGING     := $(BOILERPLATE)/.local/discord-ps5

APP_VARS := APP_SOURCE_DIR=.local/discord-ps5/src \
            APP_PARAM=.local/discord-ps5/sce_sys/param.json \
            APP_INCLUDE_PATHS=.local/discord-ps5/src

.PHONY: all app stage ffpkg test host-snapshots clean distclean

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

# Needs the builder container: Mesa's surfaceless EGL is installed there.
host-snapshots:
	@printf '%s\n' '==> [host-snapshots] rendering the screens (theme: $(THEME))'
	@scripts/dev.sh bash tools/host-snapshots.sh $(THEME)

clean:
	@rm -rf build

distclean: clean
	@rm -rf $(STAGING)
