#!/usr/bin/env bash

set -euxo pipefail

brew install --quiet --force --overwrite \
     automake scons

scons -Q \
      --enable-werror \
      --enable-tests \
      --enable-examples \
      --enable-static \
      --build-3rdparty=all \
      --macos-platform=10.12 \
      --macos-arch=all \
      test
