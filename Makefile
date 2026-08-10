CFLAGS += -std=c99 -Wall -Wextra -pedantic -Wold-style-declaration
CFLAGS += -Wmissing-prototypes -Wno-unused-parameter
CFLAGS += -I. -Imodules
CLIBS  += -lX11 -lXext -lXrender -lXinerama -lm -lXft -lpng16 -ljpeg $(shell pkg-config --cflags --libs xft xcb xcb-randr xcb-shape xcb-icccm xcb-keysyms xcb-util x11-xcb lua5.3)
PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
CC     ?= gcc

SRC = sbcwm.c sbccl.c modules/ctl.c modules/ctl_path.c modules/icons.c

all: sbcwm sbcwmctl

sbcwm: $(SRC) sbcwm.h sbcct.h modules/ctl.h modules/icons.h Makefile
	$(CC) -O3 $(CFLAGS) -o sbcwm $(SRC) $(CLIBS) $(LDFLAGS)

sbcwmctl: modules/sbcwmctl.c modules/ctl_path.c modules/ctl.h Makefile
	$(CC) -O3 $(CFLAGS) -o sbcwmctl modules/sbcwmctl.c modules/ctl_path.c

install: all
	install -Dm755 sbcwm $(DESTDIR)$(BINDIR)/sbcwm
	install -Dm755 sbcwmctl $(DESTDIR)$(BINDIR)/sbcwmctl
	mkdir -p ~/.config/sbcwm
	cp ./config.lua ~/.config/sbcwm

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/sbcwm
	rm -f $(DESTDIR)$(BINDIR)/sbcwmctl

clean:
	rm -f sbcwm sbcwmctl *.o modules/*.o

.PHONY: all install uninstall clean
