CFLAGS += -std=c99 -Wall -Wextra -pedantic -Wold-style-declaration
CFLAGS += -Wmissing-prototypes -Wno-unused-parameter
CLIBS  += -lX11 -lXext -lXinerama -lm -lXft $(shell pkg-config --cflags --libs xft xcb xcb-randr xcb-shape xcb-icccm xcb-keysyms xcb-util x11-xcb)
PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
CC     ?= gcc

all: sbcwm

config.h:
	cp config.def.h config.h

sbcwm: sbcwm.c sbcwm.h config.h Makefile
	$(CC) -O3 $(CFLAGS) -o sbcwm sbcwm.c $(CLIBS) $(LDFLAGS) 

install: all
	install -Dm755 sbcwm $(DESTDIR)$(BINDIR)/sbcwm

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/sbcwm

clean:
	rm -f sbcwm *.o config.h

.PHONY: all install uninstall clean
