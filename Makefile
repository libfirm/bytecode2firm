-include config.mak

BUILDDIR=build
GOAL = $(BUILDDIR)/reader
CPPFLAGS = -I. $(FIRM_CFLAGS)
CXXFLAGS = -Wall -W -O0 -g3
CFLAGS = -Wall -W -Wstrict-prototypes -Wmissing-prototypes -Wunreachable-code -Wlogical-op -Werror -O0 -g3 -std=c99 -pedantic
LFLAGS = $(FIRM_LIBS)
SOURCES = $(wildcard *.c) $(wildcard adt/*.c)
OBJECTS = $(addprefix build/, $(addsuffix .o, $(basename $(SOURCES))))

$(GOAL): $(OBJECTS)
	$(CC) -o $@ $^ $(LFLAGS)

$(BUILDDIR)/%.o: %.c $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILDDIR):
	$(INSTALL) -d $(BUILDDIR) $(BUILDDIR)/adt

clean:
	rm -rf $(OBJECTS) $(GOAL)
