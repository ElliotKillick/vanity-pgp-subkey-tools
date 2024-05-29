ifdef STATIC
STATIC_FLAGS = --static
endif

ifdef ASAN
ASAN_FLAGS = -fsanitize=address -fno-omit-frame-pointer -fno-common
endif

CFLAGS += `pkg-config --cflags glib-2.0 $(STATIC_FLAGS)`
LDFLAGS += `pkg-config --libs glib-2.0 $(STATIC_FLAGS)`

PROGNAME = get-compatible-pgp-subkeys

build: get-compatible-pgp-subkeys.c
	$(CC) $< -o $(PROGNAME) -O3 -Wall -Wpedantic $(CFLAGS) $(LDFLAGS) $(STATIC_FLAGS) $(ASAN_FLAGS)

clean:
	rm -f $(PROGNAME)

.PHONY: build clean
