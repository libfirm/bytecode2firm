-include config.mak

FIRM_HOME    ?= libfirm
FIRM_BUILD   ?= $(FIRM_HOME)/build/debug
FIRM_CFLAGS  ?= -I$(FIRM_HOME)/include
FIRM_LIBS    ?= -L$(FIRM_BUILD) -Wl,-R$(shell pwd)/$(FIRM_BUILD) -lfirm -lm

LIBOO_HOME   ?= liboo
LIBOO_BUILD  ?= $(LIBOO_HOME)/build
LIBOO_CFLAGS ?= -I$(LIBOO_HOME)/include/
LIBOO_LIBS   ?= -L$(LIBOO_BUILD) -Wl,-R$(shell pwd)/$(LIBOO_BUILD) -loo

INSTALL      ?= /usr/bin/install

BUILDDIR      = build
GOAL          = $(BUILDDIR)/bytecode2firm
CPPFLAGS      = -I. $(FIRM_CFLAGS) $(LIBOO_CFLAGS)
CFLAGS        = -Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -Wunreachable-code -Wlogical-op -Werror -O0 -g3 -std=c99 -pedantic
LFLAGS        = $(LIBOO_LIBS) $(FIRM_LIBS) -lm
SOURCES       = $(wildcard *.c) $(wildcard adt/*.c) $(wildcard driver/*.c)
DEPS          = $(addprefix $(BUILDDIR)/, $(addsuffix .d, $(basename $(SOURCES))))
OBJECTS       = $(addprefix $(BUILDDIR)/, $(addsuffix .o, $(basename $(SOURCES))))

Q            ?= @

.PHONY: all libfirm liboo clean

all: libfirm liboo $(GOAL)

libfirm:
	$(Q)$(MAKE) -C $(FIRM_HOME)

liboo: libfirm
	$(Q)$(MAKE) -C $(LIBOO_HOME)


-include $(DEPS)

$(GOAL): $(OBJECTS) libfirm liboo
	@echo '===> LD $@'
	$(Q)$(CC) -o $@ $(OBJECTS) $(LFLAGS)

$(BUILDDIR)/%.o: %.c $(BUILDDIR) libfirm liboo
	@echo '===> CC $<'
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -MD -MF $(addprefix $(BUILDDIR)/, $(addsuffix .d, $(basename $<))) -c -o $@ $<

$(BUILDDIR):
	$(INSTALL) -d $(BUILDDIR) $(BUILDDIR)/adt $(BUILDDIR)/driver

clean:
	rm -rf $(OBJECTS) $(GOAL) $(DEPS)
