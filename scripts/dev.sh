#!/usr/bin/env bash
# Run a command inside the PS5 builder container with this repo mounted at /work.
#
# Usage:
#   scripts/dev.sh clang-18 --version
#   scripts/dev.sh make -C samples/hello_world
#   scripts/dev.sh                 # opens a shell
#
# The image is built on first use. Override with PS5_BUILDER_IMAGE=<tag>.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${PS5_BUILDER_IMAGE:-accord-builder:local}"

if ! command -v docker >/dev/null 2>&1; then
    echo "dev.sh: docker not found in PATH" >&2
    exit 127
fi

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "dev.sh: building image $IMAGE (first run, this takes a few minutes) ..." >&2
    docker build -t "$IMAGE" "$ROOT" >&2
fi

# Allocate a TTY only when attached to one (CI / piped use must not pass -t).
runtime_flags=(--rm -i)
if [ -t 0 ] && [ -t 1 ]; then
    runtime_flags=(--rm -it)
fi

cmd=("$@")
if [ "${#cmd[@]}" -eq 0 ]; then
    cmd=(bash)
fi

# PS5_PAYLOAD_SDK comes from the image ENV; do not pass "-e PS5_PAYLOAD_SDK",
# which would blank it out when the host variable is unset.
exec docker run "${runtime_flags[@]}" \
    -v "$ROOT:/work" \
    -w /work \
    "$IMAGE" "${cmd[@]}"
