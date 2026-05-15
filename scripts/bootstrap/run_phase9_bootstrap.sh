#!/usr/bin/env bash
set -euo pipefail

./build-debug/havel --minimal --run scripts/bootstrap/compiler_self.hv
./build-debug/havel /tmp/phase9_simple_from_compiler_self.hvc
