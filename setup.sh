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

# Place a config.mak into libfirm directory, so we can get ccache benefits
rm -f libfirm/config.mak
rm -f liboo/config.mak
echo "FIRM_HOME        = ../libfirm" >> liboo/config.mak
echo "LIBFIRM_CPPFLAGS = -I\$(FIRM_HOME)/include" >> liboo/config.mak
echo "LIBFIRM_LFLAGS   = -L\$(FIRM_HOME)/build/debug -lfirm" >> liboo/config.mak
echo "LIBUNWIND_LFLAGS = -lunwind" >> liboo/config.mak
# Use ccache if you have it installed...
if which ccache > /dev/null; then
	echo "CC = ccache gcc" >> libfirm/config.mak
fi
if [ "`uname -s`" = "Darwin" ]; then
	echo "DLLEXT = .dylib" >> libfirm/config.mak
	echo "DLLEXT = .dylib" >> liboo/config.mak
fi

make "$@"

unzip "$GCJ_RT" -d rt
