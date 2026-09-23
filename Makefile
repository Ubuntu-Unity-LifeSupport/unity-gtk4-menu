# unity-gtk4-menu - export GTK4 header bar menus to the Unity global menu.

PREFIX  ?= /usr
LIBDIR  ?= $(PREFIX)/lib
DATADIR ?= $(PREFIX)/share
# systemd reads /usr/lib/environment.d, which is not multiarch - unlike LIBDIR.
ENVDIR  ?= $(PREFIX)/lib/environment.d

SONAME  = libunity-gtk4-menu.so.0
TARGET  = $(SONAME)

PKGS    = gtk4 gio-unix-2.0

# Headers only. The library must NOT link GTK or GLib: it is preloaded into
# every process on the machine, and pulling GTK4 into a GTK3 process kills it.
# Every symbol is resolved with dlsym at runtime instead. pkg-config --libs is
# deliberately absent here; check with
#     readelf -d libunity-gtk4-menu.so.0 | grep NEEDED
# which must list libc and nothing else.
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-parameter -fPIC $(shell pkg-config --cflags $(PKGS))
LDFLAGS += -shared -Wl,-soname,$(SONAME)
LIBS    = -ldl

all: $(TARGET)

$(TARGET): src/unity-gtk4-menu.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

install: check
	install -d $(DESTDIR)$(LIBDIR)
	install -m 644 $(TARGET) $(DESTDIR)$(LIBDIR)/$(SONAME)
	install -d $(DESTDIR)$(ENVDIR)
	install -m 644 data/60-unity-gtk4-menu.conf $(DESTDIR)$(ENVDIR)/
	install -d $(DESTDIR)$(DATADIR)/glib-2.0/schemas
	install -m 644 data/com.ubuntu-unity.gtk4-menu.gschema.xml \
	        $(DESTDIR)$(DATADIR)/glib-2.0/schemas/

# Fails the build if GTK or GLib crept back into NEEDED.
check: $(TARGET)
	@if readelf -d $(TARGET) | grep NEEDED | grep -qE "libgtk|libglib|libgio|libgobject"; then \
		echo "ERROR: $(TARGET) links a GTK or GLib library."; \
		echo "It is preloaded into every process; this breaks GTK3 applications."; \
		readelf -d $(TARGET) | grep NEEDED; \
		exit 1; \
	fi
	@echo "NEEDED is clean:"; readelf -d $(TARGET) | grep NEEDED

clean:
	rm -f $(TARGET)

.PHONY: all install clean check
