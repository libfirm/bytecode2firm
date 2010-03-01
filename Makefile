-include config.mak

BUILDDIR=build
GOAL = $(BUILDDIR)/reader
CPPFLAGS = -I. $(FIRM_CFLAGS)
CXXFLAGS = -Wall -W -O0 -g3
CFLAGS = -Wall -W -Wstrict-prototypes -Wmissing-prototypes -Werror -O0 -g3 -std=c99 -pedantic
LFLAGS = $(FIRM_LIBS)
SOURCES = $(wildcard *.c) $(wildcard adt/*.c)
OBJECTS = $(addprefix build/, $(addsuffix .o, $(basename $(SOURCES))))

$(GOAL): $(OBJECTS)
	$(CC) -o $@ $^ $(LFLAGS)

build/%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJECTS) $(GOAL)
