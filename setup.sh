#!/bin/bash
#
# Simple and stupid setup. This should set things up in a way suitable for
# the IPD boxes.

set -e

GCJ_VERSION="4.7"
GCJ_BIN="/usr/bin/gcj-$GCJ_VERSION"
GCJ_RT="/usr/share/java/libgcj-$GCJ_VERSION.jar"

BC2FIRM_STUFF="$GCJ_BIN $GCJ_RT /usr/include/libunwind.h"
for FILE in $BC2FIRM_STUFF; do
	if ! [ -e "$FILE" ]; then
		echo "Error: $FILE not found"
		exit 1
	fi
done

# Fetch submodules
git submodule update --init

# If /data1/user exists use that to place some build results into
DATA1DIR="/data1/`whoami`"
if test -w $DATA1DIR; then
	BUILDBASE="$DATA1DIR/builds/bc2firm"
	mkdir -p "$BUILDBASE/libfirm"
	rm -rf "$BUILDBASE/libfirm/"*
	rm -rf libfirm/build
	ln -s "$BUILDBASE/libfirm" libfirm/build
fi

make "$@"

unzip "$GCJ_RT" -d rt
