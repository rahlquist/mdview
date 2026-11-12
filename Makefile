# mdview — standalone Markdown viewer (GTK 4 + WebKitGTK + md4c)
PREFIX ?= $(HOME)/.local
CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
PKGS    := gtk4 webkitgtk-6.0
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LIBS    := $(shell pkg-config --libs $(PKGS)) -lmd4c -lmd4c-html

ifeq ($(shell pkg-config --exists $(PKGS) && echo ok 2>/dev/null),)
$(error missing build dependencies: $(PKGS) — install the gtk4 and webkitgtk-6.0 development packages)
endif

SRC := src/main.c src/render.c
OBJ := $(SRC:.c=.o)

all: mdview

mdview: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

check: mdview
	$(CC) $(CFLAGS) -Isrc -o tests/outline_test tests/outline_test.c src/render.c \
		$(shell pkg-config --libs glib-2.0) -lmd4c -lmd4c-html
	$(CC) $(CFLAGS) -Isrc -o tests/render_test tests/render_test.c src/render.c \
		$(shell pkg-config --libs glib-2.0) -lmd4c -lmd4c-html
	tests/outline_test
	tests/render_test

# build the WebKit security configuration probe (needs a display to run)
probe:
	$(CC) $(CFLAGS) -Isrc -o tests/js_markup_probe tests/js_markup_probe.c $(LIBS)

install: mdview
	install -Dm755 mdview $(DESTDIR)$(PREFIX)/bin/mdview
	install -Dm644 mdview.desktop $(DESTDIR)$(PREFIX)/share/applications/mdview.desktop
	install -Dm644 data/text-markdown.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/mimetypes/text-markdown.svg

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mdview
	rm -f $(DESTDIR)$(PREFIX)/share/applications/mdview.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/mimetypes/text-markdown.svg

clean:
	rm -f $(OBJ) mdview

.PHONY: all install uninstall clean
