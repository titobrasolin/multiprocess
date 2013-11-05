all: test1

DEBUG = -Wall -Werror

SHARED = \
	gb-supervisor.c \
	gb-supervisor.h \
	gb-dbus-daemon.c \
	gb-dbus-daemon.h

PKGS = gio-2.0 gio-unix-2.0

test1: $(SHARED) test1.c
	$(CC) -o $@.tmp $(WARNINGS) $(DEBUG) $(SHARED) test1.c $(shell pkg-config --cflags --libs $(PKGS))
	mv $@.tmp $@

clean:
	rm -f test1
