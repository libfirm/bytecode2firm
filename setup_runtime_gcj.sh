#!/bin/bash
# Extract class files from libgcj into rt directory

set -e

GCJ_VERSION="4.7"
GCJ_BIN="/usr/bin/gcj-$GCJ_VERSION"
GCJ_RT="/usr/share/java/libgcj-$GCJ_VERSION.jar"

mkdir -p build/gcj
unzip "$GCJ_RT" -d build/gcj
