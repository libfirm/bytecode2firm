-include config.mak

DLLEXT ?= .so

FIRM_HOME   ?= libfirm
FIRM_BUILD  ?= $(FIRM_HOME)/build/debug
FIRM_CFLAGS ?= -I$(FIRM_HOME)/include -I$(FIRM_HOME)/build/gen/include/libfirm
FIRM_LIBS   ?= -L$(FIRM_BUILD) -Wl,-R$(shell pwd)/$(FIRM_BUILD) -lfirm -lm
FIRM_FILE   ?= $(FIRM_BUILD)/libfirm$(DLLEXT)

LIBOO_HOME   ?= liboo
LIBOO_BUILD  ?= $(LIBOO_HOME)/build
LIBOO_CFLAGS ?= -I$(LIBOO_HOME)/include/
LIBOO_LIBS   ?= -L$(LIBOO_BUILD) -Wl,-R$(shell pwd)/$(LIBOO_BUILD) -loo
LIBOO_FILE   ?= $(LIBOO_BUILD)/liboo$(DLLEXT)

INSTALL      ?= /usr/bin/install

BUILDDIR      = build
GOAL          = $(BUILDDIR)/bytecode2firm
CPPFLAGS      = -I. $(FIRM_CFLAGS) $(LIBOO_CFLAGS)
CFLAGS        = -Wall -Wextra -Werror -Wunreachable-code -Wstrict-prototypes -O0 -g3 -std=c99
# TODO fix simplert to also use CFLAGS_GOOD
CFLAGS_GOOD   = $(CFLAGS) -pedantic -Wmissing-prototypes
LFLAGS        = $(LIBOO_LIBS) $(FIRM_LIBS) -lm
SOURCES       = $(wildcard *.c) $(wildcard adt/*.c) $(wildcard driver/*.c)
DEPS          = $(addprefix $(BUILDDIR)/, $(addsuffix .d, $(basename $(SOURCES))))
OBJECTS       = $(addprefix $(BUILDDIR)/, $(addsuffix .o, $(basename $(SOURCES))))

SIMPLERT_JAVA_SOURCES = $(shell find simplert/java -name "*.java")
SIMPLERT_CLASSES = $(SIMPLERT_DIR)/java/lang/Object.class # just a representative file
SIMPLERT_C_SOURCES = $(shell find simplert/c -name "*.c")
SIMPLERT_HEADERS = $(shell find simplert/c -name "*.h")
SIMPLERT_DIR = $(BUILDDIR)/simplert
SIMPLERT_dll = $(SIMPLERT_DIR)/libsimplert$(DLLEXT)
SIMPLERT_a = $(SIMPLERT_DIR)/libsimplert.a
SIMPLERT_CFLAGS ?= -m32 -W
SIMPLERT_LINKFLAGS ?= -shared -lm

GCJ_DIR = $(BUILDDIR)/gcj

UNUSED := $(shell mkdir -p $(BUILDDIR) $(BUILDDIR)/adt $(BUILDDIR)/driver)

CLASSPATH_SIMPLERT ?= -DCLASSPATH_SIMPLERT=\"$(abspath $(SIMPLERT_DIR))\"
CLASSPATH_GCJ ?= -DCLASSPATH_GCJ=\"$(abspath $(GCJ_DIR))\"
CPPFLAGS += $(CLASSPATH_SIMPLERT) $(CLASSPATH_GCJ)

TESTDIR = testsuite

# This hides the noisy commandline outputs. Show them with "make V=1"
ifneq ($(V),1)
Q ?= @
endif

.PHONY: all libfirm liboo clean distclean test

all: $(GOAL) $(SIMPLERT_dll) $(SIMPLERT_a) $(SIMPLERT_CLASSES)

libfirm:
	$(Q)$(MAKE) -C $(FIRM_HOME)

liboo: libfirm
	$(Q)$(MAKE) -C $(LIBOO_HOME)

$(FIRM_FILE): libfirm

$(LIBOO_FILE): liboo

# re-evaluate Makefile to make sure changes/dependencies from the subdirs are
# picked up in this run already
Makefile: libfirm liboo

-include $(DEPS)

$(GOAL): $(OBJECTS) $(FIRM_FILE) $(LIBOO_FILE)
	@echo '===> LD $@'
	$(Q)$(CC) -o $@ $(OBJECTS) $(LFLAGS)

$(BUILDDIR)/%.o: %.c
	@echo '===> CC $<'
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS_GOOD) -MP -MMD -c -o $@ $<

$(SIMPLERT_dll): $(SIMPLERT_C_SOURCES) $(SIMPLERT_HEADERS)
	@echo '===> CC $@'
	$(Q)mkdir -p $(SIMPLERT_DIR)
	$(Q)$(CC) $(CFLAGS) $(SIMPLERT_CFLAGS) $(SIMPLERT_C_SOURCES) $(SIMPLERT_LINKFLAGS) -o $@

SIMPLERT_SOURCES_ABS=$(abspath $(SIMPLERT_C_SOURCES))
$(SIMPLERT_a): $(SIMPLERT_C_SOURCES) $(SIMPLERT_HEADERS)
	@echo '===> CC+AR $@'
	$(Q)mkdir -p $(SIMPLERT_DIR)
	$(Q)cd $(SIMPLERT_DIR) && $(CC) $(CFLAGS) $(SIMPLERT_CFLAGS) -c $(SIMPLERT_SOURCES_ABS)
	$(Q)ar rcs $@ $(SIMPLERT_DIR)/*.o

$(SIMPLERT_CLASSES): $(SIMPLERT_JAVA_SOURCES)
	@echo '===> JAVAC all runtime classes'
	$(Q)mkdir -p $(SIMPLERT_DIR)
	$(Q)javac -d $(SIMPLERT_DIR) $(SIMPLERT_JAVA_SOURCES)

clean:
	$(Q)rm -rf $(BUILDDIR)/*

distclean: clean
	$(Q)$(MAKE) -C $(FIRM_HOME) clean
	$(Q)$(MAKE) -C $(LIBOO_HOME) clean

$(TESTDIR)/.git/config:
	$(Q)git clone http://pp.ipd.kit.edu/git/bytecode2firm-testsuite/ $(TESTDIR) --recursive

test: $(GOAL) $(TESTDIR)/.git/config
	$(Q)cd $(TESTDIR); sisyphus/sis --bc2firm ../$(GOAL) --expect fail_expectations
