-include config.mak

BUILDDIR=build
GOAL = $(BUILDDIR)/bytecode2firm
CPPFLAGS = -I. $(FIRM_CFLAGS) $(LIBOO_CFLAGS)
CFLAGS = -Wall -W -Wstrict-prototypes -Wmissing-prototypes -Wunreachable-code -Wlogical-op -Werror -O0 -g3 -std=c99 -pedantic
LFLAGS = $(LIBOO_LIBS) $(FIRM_LIBS)
SOURCES = $(wildcard *.c) $(wildcard adt/*.c)
DEPS = $(addprefix $(BUILDDIR)/, $(addsuffix .d, $(basename $(SOURCES))))
OBJECTS = $(addprefix $(BUILDDIR)/, $(addsuffix .o, $(basename $(SOURCES))))

Q ?= @

.phony: liboo
liboo:
	$(MAKE) -C $(LIBOO_HOME)
	$(MAKE) $(GOAL)

all: liboo 

-include $(DEPS)

$(GOAL): $(OBJECTS)
	@echo '===> LD $@'
	$(Q)$(CC) -o $@ $^ $(LFLAGS)

$(BUILDDIR)/%.o: %.c $(BUILDDIR)
	@echo '===> CC $<'
	$(Q)#cparser $(CPPFLAGS) $(CFLAGS) -fsyntax-only $<
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -MD -MF $(addprefix $(BUILDDIR)/, $(addsuffix .d, $(basename $<))) -c -o $@ $<

$(BUILDDIR):
	$(INSTALL) -d $(BUILDDIR) $(BUILDDIR)/adt

clean:
	rm -rf $(OBJECTS) $(GOAL) $(DEPS)
