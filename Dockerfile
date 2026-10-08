# T1.1 — builder container for the PS5 payload SDK (project: Discord PS5)
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
