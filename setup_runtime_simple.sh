#!/bin/bash
# Compiles our stupid 99% incomplete runtime

set -e

mkdir -p classes rt

pushd simplert > /dev/null
./build.sh
popd > /dev/null
