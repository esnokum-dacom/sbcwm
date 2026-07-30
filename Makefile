CFLAGS += -std=c99 -Wall -Wextra -pedantic -Wold-style-declaration
CFLAGS += -Wmissing-prototypes -Wno-unused-parameter
CLIBS  += -lX11 -lXext -lXinerama -lm -lXft $(shell pkg-config --cflags --libs xft xcb xcb-randr xcb-shape xcb-icccm xcb-keysyms xcb-util x11-xcb lua5.3)
PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
CC     ?= gcc

all: sbcwm

sbcwm: sbcwm.c sbcwm.h sbccl.c sbcct.h Makefile
	$(CC) -O3 $(CFLAGS) -o sbcwm sbcwm.c sbccl.c $(CLIBS) $(LDFLAGS) 

install: all
	install -Dm755 sbcwm $(DESTDIR)$(BINDIR)/sbcwm
	mkdir -p ~/.config/sbcwm
	cp ./config.lua ~/.config/sbcwm

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/sbcwm

clean:
	rm -f sbcwm *.o

.PHONY: all install uninstall clean
