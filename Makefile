-include config.mak

FIRM_HOME   ?= libfirm
FIRM_BUILD  ?= $(FIRM_HOME)/build/debug
FIRM_CFLAGS ?= -I$(FIRM_HOME)/include -I$(FIRM_HOME)/build/gen/include/libfirm
FIRM_LIBS   ?= -L$(FIRM_BUILD) -Wl,-R$(shell pwd)/$(FIRM_BUILD) -lfirm -lm
FIRM_FILE   ?= $(FIRM_BUILD)/libfirm.so

LIBOO_HOME   ?= liboo
LIBOO_BUILD  ?= $(LIBOO_HOME)/build
LIBOO_CFLAGS ?= -I$(LIBOO_HOME)/include/
LIBOO_LIBS   ?= -L$(LIBOO_BUILD) -Wl,-R$(shell pwd)/$(LIBOO_BUILD) -loo
LIBOO_FILE   ?= $(LIBOO_BUILD)/liboo.so

INSTALL      ?= /usr/bin/install

BUILDDIR      = build
GOAL          = $(BUILDDIR)/bytecode2firm
CPPFLAGS      = -I. $(FIRM_CFLAGS) $(LIBOO_CFLAGS)
CFLAGS        = -Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -Wunreachable-code -Wlogical-op -Werror -O0 -g3 -std=c99 -pedantic
LFLAGS        = $(LIBOO_LIBS) $(FIRM_LIBS) -lm
SOURCES       = $(wildcard *.c) $(wildcard adt/*.c) $(wildcard driver/*.c)
DEPS          = $(addprefix $(BUILDDIR)/, $(addsuffix .d, $(basename $(SOURCES))))
OBJECTS       = $(addprefix $(BUILDDIR)/, $(addsuffix .o, $(basename $(SOURCES))))

SIMPLERT_JAVA_SOURCES = $(shell find simplert/java -name "*.java")
SIMPLERT_C_SOURCES = $(shell find simplert/c -name "*.c") $(shell find simplert/c -name "*.h")
SIMPLERT_SOURCES = $(SIMPLERT_C_SOURCES) $(SIMPLERT_JAVA_SOURCES)

UNUSED := $(shell mkdir -p $(BUILDDIR) $(BUILDDIR)/adt $(BUILDDIR)/driver)

DEFAULT_BOOTCLASSPATH ?= -DDEFAULT_BOOTCLASSPATH=\"$(abspath .)/rt\"
CPPFLAGS += $(DEFAULT_BOOTCLASSPATH)

# This hides the noisy commandline outputs. Show them with "make V=1"
ifneq ($(V),1)
Q ?= @
endif

.PHONY: all libfirm liboo clean distclean

all: $(GOAL) rt/libsimplert.so rt/simplert.a rt/java/lang/Object.class

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
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -MP -MMD -c -o $@ $<

rt/libsimplert.so: $(SIMPLERT_C_SOURCES)
	@echo '===> CC $@'
	$(Q)mkdir -p rt
	$(Q)$(CC) -std=c99 -Wall -W -m32 -g3 -shared $(SIMPLERT_C_SOURCES) -o $@

rt/simplert.a: $(SIMPLERT_C_SOURCES)
	@echo '===> CC+AR $@'
	$(Q)mkdir -p rt build/rt
	$(Q)cd build/rt && $(CC) -std=c99 -Wall -W -m32 -g3 -c $(addprefix ../../, $(SIMPLERT_C_SOURCES))
	$(Q)ar rcs $@ build/rt//*.o

rt/java/lang/Object.class: $(SIMPLERT_JAVA_SOURCES)
	@echo '===> JAVAC all runtime classes'
	$(Q)mkdir -p rt
	$(Q)javac -d rt $(SIMPLERT_JAVA_SOURCES)

clean:
	$(Q)rm -rf $(OBJECTS) $(GOAL) $(DEPS) $(BUILDDIR) rt/*

distclean: clean
	$(Q)$(MAKE) -C $(FIRM_HOME) clean
	$(Q)$(MAKE) -C $(LIBOO_HOME) clean
