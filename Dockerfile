# T1.1 — builder container for the PS5 payload SDK (project: Accord)
#
# Base image follows the SDK's own CI (.github/workflows/ubuntu-latest.yml in
# ps5-payload-dev/sdk), which builds on ubuntu-latest with clang-18 + lld-18.
# The README's required packages are: bash clang-18 lld-18 wget.
#
# The SDK is installed from a pinned release, not built here.

FROM ubuntu:24.04

ARG SDK_VERSION=v0.43

ENV DEBIAN_FRONTEND=noninteractive \
    PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk \
    PATH=/opt/ps5-payload-sdk/bin:/usr/lib/llvm-18/bin:${PATH}

RUN apt-get update && apt-get install -y --no-install-recommends \
        bash \
        ca-certificates \
        curl \
        clang-18 \
        lld-18 \
        llvm-18 \
        make \
        cmake \
        ninja-build \
        meson \
        pkg-config \
        python3 \
        python3-pyelftools \
        wget \
        unzip \
        zip \
        git \
        socat \
        ccache \
    && rm -rf /var/lib/apt/lists/*

# The SDK targets x86_64 (prospero), but on an arm64 host clang-18 ships only
# the aarch64 compiler-rt builtins, so a link fails with
# "missing link input: .../libclang_rt.builtins-x86_64.a".
# Pulling the amd64 builtins through multiarch keeps clang itself running
# natively (much faster than emulating the whole build via --platform).
# This base image is arm64, so apt points at ports.ubuntu.com, which carries no
# amd64 packages: the arm64 stanzas are pinned to arm64 and an amd64-only
# sources file is added against archive.ubuntu.com.
RUN sed -i 's/^Types: deb$/Types: deb\nArchitectures: arm64/' /etc/apt/sources.list.d/ubuntu.sources \
    && printf '%s\n' \
        'Types: deb' \
        'Architectures: amd64' \
        'URIs: http://archive.ubuntu.com/ubuntu/' \
        'Suites: noble noble-updates noble-backports noble-security' \
        'Components: main universe restricted multiverse' \
        'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg' \
        > /etc/apt/sources.list.d/amd64.sources \
    && dpkg --add-architecture amd64 \
    && apt-get update \
    && apt-get install -y --no-install-recommends libclang-rt-18-dev:amd64 \
    && test -f /usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.builtins-x86_64.a \
    && rm -rf /var/lib/apt/lists/*

# Host preview (T1.3): ps5-homebrew-ui renders off-screen through Mesa's
# surfaceless EGL and links -lEGL -lGL. llvmpipe (in libgl1-mesa-dri) is the
# software renderer used in a VM without a GPU.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libegl-dev \
        libgl-dev \
        libglvnd-dev \
        libegl-mesa0 \
        libgl1-mesa-dri \
        libgbm1 \
    && rm -rf /var/lib/apt/lists/*

# Packaging (T1.6): the `ffpkg` target builds UFS2Tool, and
# tools/setup-packaging-dependencies.sh requires the .NET SDK (>= 8) to compile
# it. Ubuntu 24.04 ships it in universe; the amd64/arm64 sources above already
# cover the architectures this base image needs.
RUN apt-get update && apt-get install -y --no-install-recommends \
        dotnet-sdk-8.0 \
    && rm -rf /var/lib/apt/lists/*

# Pinned ps5-payload-sdk release (see THIRD_PARTY.md / report for the hash).
RUN wget -q "https://github.com/ps5-payload-dev/sdk/releases/download/${SDK_VERSION}/ps5-payload-sdk.zip" \
        -O /tmp/ps5-payload-sdk.zip \
    && mkdir -p /opt \
    && unzip -q /tmp/ps5-payload-sdk.zip -d /opt \
    && rm -f /tmp/ps5-payload-sdk.zip \
    && test -f "${PS5_PAYLOAD_SDK}/toolchain/prospero.sh"

WORKDIR /work

# Sanity check baked into the image: toolchain must report version 18.
RUN clang-18 --version \
    && ld.lld-18 --version \
    && test -f "${PS5_PAYLOAD_SDK}/toolchain/prospero.sh"
