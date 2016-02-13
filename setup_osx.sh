#!/bin/bash
# Setup compilation optins for Mac OS X

if [ "$1" != "-f" ]; then
	# Don't accidently override the users config.mak files
	CONFIGS="config.mak libfirm/config.mak liboo/config.mak"
	for f in $CONFIGS; do
		if [ -e "$f" ]; then
			echo "Error: One or more of these files already exist $CONFIGS (override wiht -f)"
			exit 1
		fi
	done
fi

cat > config.mak << '__END__'
FIRM_FILE = $(FIRM_BUILD)/libfirm.a
FIRM_LIBS  = $(FIRM_FILE) -lm
LIBOO_FILE = $(LIBOO_BUILD)/liboo.a
LIBOO_LIBS = $(LIBOO_FILE)
SIMPLERT_LINKFLAGS = -shared -lm -undefined dynamic_lookup
DLLEXT = .dylib
__END__

cat > liboo/config.mak << '__END__'
FIRM_HOME        = ../libfirm
LIBFIRM_CPPFLAGS = -I$(FIRM_HOME)/include -I$(FIRM_HOME)/build/gen/include/libfirm
LIBFIRM_LFLAGS   = -L$(FIRM_HOME)/build/debug -lfirm
TARGET_CC        = cc
DLLEXT           = .dylib
__END__

cat > libfirm/config.mak << '__END__'
DLLEXT = .dylib
__END__
