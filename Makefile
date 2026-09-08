CC = clang
CFLAGS = -std=c11 -Os -Wall -Wextra -Werror -mmacosx-version-min=14.0
FRAMEWORKS = -framework CoreFoundation -framework IOKit -framework ApplicationServices
SOURCES = $(wildcard src/*.c)
PREFIX ?= /usr/local

.PHONY: all test sanitize clean install
all: build/dshot
build/dshot: $(SOURCES) src/dshot.h
	mkdir -p build
	$(CC) $(CFLAGS) $(SOURCES) $(FRAMEWORKS) -o $@
	codesign --force --sign - $@
build/dshot-test: $(SOURCES) src/dshot.h
	mkdir -p build
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-unused-variable -DDSHOT_TEST $(SOURCES) $(FRAMEWORKS) -o $@
test: build/dshot build/dshot-test
	python3 tests/integration.py
build/dshot-sanitize: $(SOURCES) src/dshot.h
	mkdir -p build
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -Wno-unused-function -Wno-unused-variable -DDSHOT_TEST $(SOURCES) $(FRAMEWORKS) -o $@
sanitize: build/dshot-sanitize
	DSHOT_TEST_BINARY=$(CURDIR)/build/dshot-sanitize python3 tests/integration.py
install: build/dshot
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/man/man1
	install -m 755 build/dshot $(DESTDIR)$(PREFIX)/bin/dshot
	install -m 644 share/dshot.1 $(DESTDIR)$(PREFIX)/share/man/man1/dshot.1
clean:
	rm -rf build
