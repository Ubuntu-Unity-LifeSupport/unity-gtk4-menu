# unity-gtk4-menu - export GTK4 header bar menus to the Unity global menu.

PREFIX  ?= /usr
LIBDIR  ?= $(PREFIX)/lib
DATADIR ?= $(PREFIX)/share
# systemd reads /usr/lib/environment.d, which is not multiarch - unlike LIBDIR.
ENVDIR  ?= $(PREFIX)/lib/environment.d

SONAME  = libunity-gtk4-menu.so.0
TARGET  = $(SONAME)

PKGS    = gtk4 gio-unix-2.0
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -fPIC $(shell pkg-config --cflags $(PKGS))
LDFLAGS += -shared -Wl,-soname,$(SONAME)
LIBS    = $(shell pkg-config --libs $(PKGS)) -ldl

all: $(TARGET)

$(TARGET): src/unity-gtk4-menu.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

install: all
	install -d $(DESTDIR)$(LIBDIR)
	install -m 644 $(TARGET) $(DESTDIR)$(LIBDIR)/$(SONAME)
	install -d $(DESTDIR)$(ENVDIR)
	install -m 644 data/60-unity-gtk4-menu.conf $(DESTDIR)$(ENVDIR)/
	install -d $(DESTDIR)$(DATADIR)/glib-2.0/schemas
	install -m 644 data/com.ubuntu-unity.gtk4-menu.gschema.xml \
	        $(DESTDIR)$(DATADIR)/glib-2.0/schemas/

clean:
	rm -f $(TARGET)

.PHONY: all install clean
