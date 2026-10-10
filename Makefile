# Accord - build entry point.
#
# Layout:
#   core/           pure logic, no PS5 SDK -- compiles with the host toolchain
#   platform/ps5/   PS5 implementation of the platform seams
#   platform/host/  host substitutes: the test sinks and the snapshot runner
#   ui/             screens, and the vendored drawing kit under ui/kit/
#   daemon/         the background payload: a second artifact from this tree
#   tests/          host tests
#   sce_sys/        title identity (param.json)
#
# Targets:
#   make            build the PS5 application (needs the builder container)
#   make test       build and run the host tests (native toolchain, no container)
#   make host-snapshots  render every screen to build/snapshots/*.png (container, Mesa)
#   make payload    build the background daemon to dist/accordd.elf (container)
#   make dave-deps  cross-build OpenSSL 3, mlspp and libdave for the payload (container)
#   make dave-host-test  native libdave suite plus the facade smoke test (container)
#   make ffpkg      build the PS5 application and its UFS2 image, copied to dist/
#   make stage      only stage the sources into the boilerplate harness
#   make clean      drop host build output
#   make distclean  also drop the staged tree inside the boilerplate

CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic

# snapshot_main.cpp and the daemon's main.cpp carry a main() of their own: one is
# the snapshot runner, the other the payload entry point, neither is a test.
# audio_ps5.cpp links against libSceAudioOut, so it stays out of the host build.
HOST_SOURCES := $(shell find core platform/host daemon tests -name '*.cpp' \
                   ! -name 'snapshot_main.cpp' ! -name 'main.cpp' ! -name 'audio_ps5.cpp' | sort)
HOST_BINARY  := build/host-tests

# Which of the kit's themes the snapshots are rendered in.
THEME ?= acrylic

# The boilerplate harness builds our code, because it is the only one of the two
# submodules that accepts outside sources (APP_SOURCE_DIR/APP_PARAM).
BOILERPLATE := third_party/ps5-native-app-boilerplate
TITLE_ID    := PPSA99070
STAGING     := $(BOILERPLATE)/.local/accord

APP_VARS := APP_SOURCE_DIR=.local/accord/src \
            APP_PARAM=.local/accord/sce_sys/param.json \
            APP_INCLUDE_PATHS=.local/accord/src

.PHONY: all app stage ffpkg test host-snapshots payload dave-deps dave-host-test clean distclean

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

# The daemon is a payload, not a title: the bare SDK builds it rather than the
# boilerplate harness, and it lands in dist/ as a single .elf.
payload:
	@printf '%s\n' '==> [payload] background daemon (elf) in the builder container'
	@scripts/dev.sh bash tools/build-daemon-payload.sh

# DAVE (T6.0-2). The dependency chain is fetched and cross-built into build/dave,
# which is gitignored, so this runs once per checkout and `make payload` needs it.
# Each step is a separate container: the sources and the install prefix live in
# the mounted tree, not in the container.
dave-deps:
	@printf '%s\n' '==> [dave-deps] fetching the pinned sources'
	@scripts/dev.sh bash tools/dave/fetch-sources.sh
	@printf '%s\n' '==> [dave-deps] OpenSSL 3 for the payload toolchain'
	@scripts/dev.sh bash tools/dave/build-openssl.sh
	@printf '%s\n' '==> [dave-deps] nlohmann/json and mlspp for the payload toolchain'
	@scripts/dev.sh bash tools/dave/build-mlspp.sh
	@printf '%s\n' '==> [dave-deps] libdave for the payload toolchain'
	@scripts/dev.sh bash tools/dave/build-libdave.sh

# The same libraries built natively, so libdave's own suite and the facade can be
# run without a console. Needs libssl-dev, libgtest-dev and libgmock-dev in the
# image; the host test of the facade belongs to this target, not to `make test`,
# because it drags in a whole native dependency build.
dave-host-test:
	@printf '%s\n' '==> [dave-host-test] native libdave suite and the facade smoke test'
	@scripts/dev.sh bash tools/dave/build-host.sh
	@scripts/dev.sh bash tools/dave/facade-host-smoke.sh

clean:
	@rm -rf build

distclean: clean
	@rm -rf $(STAGING)
