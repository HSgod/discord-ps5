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
#   make ws-soak    ten minutes of WebSocket frames over loopback TCP (log in build/)
#   make gateway-hello  build the manual gateway tool of T3.4 (run it by hand)
#   make host-snapshots  render every screen to build/snapshots/*.png (container, Mesa)
#   make payload    build the background daemon to dist/accordd.elf (container)
#   make dave-deps  cross-build OpenSSL 3, mlspp and libdave for the payload (container)
#   make dave-host-test  native libdave suite plus the facade smoke test (container)
#   make ffpkg      build the PS5 application and its UFS2 image, copied to dist/
#   make stage      only stage the sources into the boilerplate harness
#   make opengl     fetch and prepare the OpenGL SDK the kit links against
#   make clean      drop host build output
#   make distclean  also drop the staged tree inside the boilerplate

CXX      ?= c++
CC       ?= cc
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic

# snapshot_main.cpp and the daemon's main.cpp carry a main() of their own: one is
# the snapshot runner, the other the payload entry point, neither is a test.
# audio_ps5.cpp links against libSceAudioOut, so it stays out of the host build.
# Of ui/ only the navigation is host-clean: it is the shell's input logic, with
# no kit and no SDK in it, which is why it is listed by hand. The screens and
# the drawing code need the kit and are exercised by `make host-snapshots`.
# tests/live holds tools with a main() of their own that talk to servers on the
# internet; they are run by hand and are not part of the suite (see below).
HOST_SOURCES := $(shell find core platform/host platform/net daemon tests -name '*.cpp' \
                   ! -path 'tests/live/*' \
                   ! -name 'snapshot_main.cpp' ! -name 'main.cpp' ! -name 'audio_ps5.cpp' | sort) \
                ui/nav.cpp
HOST_BINARY  := build/host-tests

# The gateway tool of T3.4 is the other side of the same line: it is a host
# binary, but what it checks is the real Discord, so it is never part of `make
# test`. Anything that talks to a server nobody in this repository runs belongs
# in tests/live and gets its own target.
LIVE_SOURCES := $(shell find core platform/net tests/live -name '*.cpp' | sort)
LIVE_BINARY  := build/gateway-hello

# The ten-minute soak of the WebSocket client is not a host test: it takes ten
# minutes and prints a log rather than an exit status, so it links only what it
# needs and `make test` stays as fast as it was.
SOAK_SOURCES := $(shell find core platform/net tests/support -name '*.cpp' | sort) \
                tests/soak/main.cpp
SOAK_BINARY  := build/ws-soak

# core/json.cpp parses with yyjson, fetched and pinned by tools/fetch-yyjson.sh
# into third_party/yyjson (not committed). The pinned parser is a prerequisite
# of every target that compiles core/, so the fetch happens by itself the first
# time and is a no-op afterwards.
JSON_LIB   := third_party/yyjson
JSON_OBJ   := build/yyjson.o

$(JSON_LIB)/yyjson.c:
	@bash tools/fetch-yyjson.sh

.PHONY: json-lib

json-lib: $(JSON_LIB)/yyjson.c

# C, not C++: the parser is a C99 library, and core/json.cpp keeps it behind
# its own header so nothing else in the tree sees it.
$(JSON_OBJ): $(JSON_LIB)/yyjson.c
	@mkdir -p $(@D)
	$(CC) -O2 -Wall -Wextra -c $(JSON_LIB)/yyjson.c -o $@

# core/zlib_stream.cpp inflates the gateway's `compress=zlib-stream`, with the
# zlib pinned by tools/fetch-zlib.sh into third_party/zlib (not committed). Like
# the parser above it is C, and it is fetched by itself.
#
# What is compiled here is the inflate side: the six files that answer for
# inflateInit/inflate/inflateEnd and what they call. The compressor is a test
# dependency -- the host tests make their own Z_SYNC_FLUSH fixtures and then read
# them back with the same library -- so deflate.c and trees.c go into the tests
# alone and no shipped artifact carries a compressor it never calls.
ZLIB_LIB     := third_party/zlib
ZLIB_OBJ_DIR := build/zlib
ZLIB_INFLATE := adler32.c crc32.c inffast.c inflate.c inftrees.c zutil.c
ZLIB_OBJECTS := $(patsubst %.c,$(ZLIB_OBJ_DIR)/%.o,$(ZLIB_INFLATE))
ZLIB_FIXTURE_OBJECTS := $(ZLIB_OBJ_DIR)/deflate.o $(ZLIB_OBJ_DIR)/trees.o

.PHONY: zlib-lib

# The fetch brings the whole release at once, so any file that is not there yet
# is a reason to run it -- and, once it is, a no-op.
$(ZLIB_LIB)/%:
	@bash tools/fetch-zlib.sh

zlib-lib: $(ZLIB_LIB)/zlib.h

$(ZLIB_OBJ_DIR)/%.o: $(ZLIB_LIB)/%.c
	@mkdir -p $(@D)
	$(CC) -O2 -Wall -Wextra -c $< -o $@

# platform/net/tls_stream.cpp is TLS on OpenSSL 3.5.2. The daemon links the pair
# cross-built for the console toolchain (tools/dave/build-openssl.sh); the host
# tests link their own native build of the same version, so that what they check
# is what ships and not whatever OpenSSL the machine happens to have. Both land
# in build/, which git ignores, and both arrive by themselves -- the first
# `make test` on a fresh checkout pays for the host build once.
OPENSSL_HOST := build/openssl-host/prefix
OPENSSL_HOST_LIBS := $(OPENSSL_HOST)/lib/libssl.a $(OPENSSL_HOST)/lib/libcrypto.a

.PHONY: openssl-host

# Same shape as the rules above: anything missing from the prefix is a reason to
# run the build script, which does nothing when the prefix is already there.
$(OPENSSL_HOST)/lib/%.a:
	@bash tools/build-openssl-host.sh

openssl-host: $(OPENSSL_HOST_LIBS)

# The roots the TLS client verifies against: Mozilla's, as curl publishes them,
# fetched at a pinned extraction date by tools/fetch-cacert.sh into
# third_party/cacert (not committed). platform/net/ca_bundle.hpp reads the
# embedded copy, which tools/embed-cacert.sh generates from that file, so the
# host tests, the daemon and anything later all carry the same roots.
CACERT_LIB := third_party/cacert
CA_SRC     := build/cacert/ca_bundle.cpp
CA_OBJ     := build/cacert/ca_bundle.o

.PHONY: cacert-lib

$(CACERT_LIB)/%:
	@bash tools/fetch-cacert.sh

cacert-lib: $(CACERT_LIB)/cacert.pem

$(CA_SRC): $(CACERT_LIB)/cacert.pem tools/embed-cacert.sh
	@bash tools/embed-cacert.sh $@

# -Wno-overlength-strings: the bundle is 189 KB inside one character array, which
# is the point of embedding it, and that warning says exactly that and nothing
# else. The generated file repeats this at the top of itself.
$(CA_OBJ): $(CA_SRC)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Wno-overlength-strings -I. -c $< -o $@

# Which of the kit's themes the snapshots are rendered in.
THEME ?= acrylic

# The boilerplate harness builds our code, because it is the only one of the two
# submodules that accepts outside sources (APP_SOURCE_DIR/APP_PARAM).
BOILERPLATE := third_party/ps5-native-app-boilerplate
TITLE_ID    := PPSA99070
STAGING     := $(BOILERPLATE)/.local/accord

# The kit draws with OpenGL, so the title needs the ps5-opengl SDK: its headers
# on the include path, GL entry points visible as prototypes, and the link group
# tools/prepare-opengl.sh writes. The harness resolves each of these relative to
# its own root, which is the parent of both our tree and the SDK.
OPENGLS := third_party/ps5-opengl

# --wrap is what routes the process's allocations into the arena
# platform/ps5/app_heap.c keeps (and the splash hold in opengl_runtime_shims.c
# needs its own wrap). The harness links the wrappers only because the symbols
# are listed here: drop a name and its __wrap_* is dead code.
APP_VARS := APP_SOURCE_DIR=.local/accord/src \
            APP_PARAM=.local/accord/sce_sys/param.json \
            'APP_INCLUDE_PATHS=../ps5-opengl/sdk/include .local/accord/src' \
            APP_DEFINITIONS=GL_GLEXT_PROTOTYPES=1 \
            APP_STATIC_ARCHIVES=../ps5-opengl/libps5opengl-group.a \
            'APP_WRAP_SYMBOLS=sceSystemServiceHideSplashScreen malloc calloc realloc free posix_memalign malloc_usable_size' \
            APP_ASSETS=.local/accord/assets

.PHONY: all app stage opengl ffpkg test ws-soak gateway-hello host-snapshots payload dave-deps \
        dave-host-test clean distclean

all: app

stage: $(JSON_LIB)/yyjson.c $(ZLIB_LIB)/zlib.h
	@bash tools/stage-ps5-sources.sh

# Inside the container: the payload SDK it copies the AGC stubs into, and the
# clang-18 it takes the builtins from, are the container's.
opengl:
	@printf '%s\n' '==> [opengl] ps5-opengl SDK and link group'
	@scripts/dev.sh bash tools/prepare-opengl.sh

app: stage opengl
	@printf '%s\n' '==> [app] PS5 build in the builder container'
	@scripts/dev.sh make -C $(BOILERPLATE) app $(APP_VARS)

ffpkg: stage opengl
	@printf '%s\n' '==> [ffpkg] PS5 build and UFS2 image'
	@scripts/dev.sh make -C $(BOILERPLATE) ffpkg $(APP_VARS)
	@mkdir -p dist
	@cp -a $(BOILERPLATE)/dist/$(TITLE_ID).ffpkg dist/
	@printf '%s\n' "==> [ffpkg] dist/$(TITLE_ID).ffpkg"

$(HOST_BINARY): $(HOST_SOURCES) $(JSON_OBJ) $(ZLIB_OBJECTS) $(ZLIB_FIXTURE_OBJECTS) $(CA_OBJ) \
                $(OPENSSL_HOST_LIBS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I. -I$(OPENSSL_HOST)/include $(HOST_SOURCES) $(JSON_OBJ) $(ZLIB_OBJECTS) \
	    $(ZLIB_FIXTURE_OBJECTS) $(CA_OBJ) $(OPENSSL_HOST)/lib/libssl.a \
	    $(OPENSSL_HOST)/lib/libcrypto.a -o $@

test: $(HOST_BINARY)
	@printf '%s\n' '==> [test] host unit tests'
	@./$(HOST_BINARY)

# The soak is not about TLS, but it compiles the same platform/net and
# tests/support sources `make test` does -- that is what its glob is -- so it
# needs OpenSSL's headers and archives as well to link.
$(SOAK_BINARY): $(SOAK_SOURCES) $(JSON_OBJ) $(ZLIB_OBJECTS) $(OPENSSL_HOST_LIBS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I. -I$(OPENSSL_HOST)/include $(SOAK_SOURCES) $(JSON_OBJ) $(ZLIB_OBJECTS) \
	    $(OPENSSL_HOST)/lib/libssl.a $(OPENSSL_HOST)/lib/libcrypto.a -o $@

# Ten minutes of frames over loopback TCP, on one connection. The log of a run
# is what T3.3 asks for, so it is kept: build/ws-soak.log.
ws-soak: $(SOAK_BINARY)
	@printf '%s\n' '==> [ws-soak] ten minutes of frames on one connection (log: build/ws-soak.log)'
	@./$(SOAK_BINARY) 10 | tee build/ws-soak.log

# The gateway tool of T3.4 is built like the soak and run like nothing else in
# this file: by hand, against discord.com, with the pinned roots compiled in.
# It is deliberately not a dependency of `make test` -- a suite whose result
# depends on someone else's server passing or failing is not a suite -- so this
# target builds it and stops, and the tool itself is what the run looks at.
$(LIVE_BINARY): $(LIVE_SOURCES) $(JSON_OBJ) $(ZLIB_OBJECTS) $(CA_OBJ) $(OPENSSL_HOST_LIBS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I. -I$(OPENSSL_HOST)/include $(LIVE_SOURCES) $(JSON_OBJ) $(ZLIB_OBJECTS) \
	    $(CA_OBJ) $(OPENSSL_HOST)/lib/libssl.a $(OPENSSL_HOST)/lib/libcrypto.a -o $@

gateway-hello: $(LIVE_BINARY)
	@printf '%s\n' '==> [gateway-hello] built; run it by hand:'
	@printf '%s\n' '    $(LIVE_BINARY)                 the gateway without compression'
	@printf '%s\n' '    $(LIVE_BINARY) --zlib-stream   with compress=zlib-stream'

# Needs the builder container: Mesa's surfaceless EGL is installed there.
host-snapshots:
	@printf '%s\n' '==> [host-snapshots] rendering the screens (theme: $(THEME))'
	@scripts/dev.sh bash tools/host-snapshots.sh $(THEME)

# The daemon is a payload, not a title: the bare SDK builds it rather than the
# boilerplate harness, and it lands in dist/ as a single .elf.
payload: $(JSON_LIB)/yyjson.c $(ZLIB_LIB)/zlib.h $(CACERT_LIB)/cacert.pem
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
