CC := gcc

CFLAGS  := -O3 -g -Wall -Iinclude/aeron/ -std=c17 -Wshadow -Wformat=2 -Wextra -Wunused
LDFLAGS := -Llib/ -lpthread -laeron_static

SOURCES := $(filter-out src/pub.c src/sub.c, $(wildcard src/*.c))
OBJECTS := $(subst src/,build/,$(SOURCES:.c=.o))

.PHONY: build deps devel-build

default: build build/aeron-bench-pub build/aeron-bench-sub

build:
	mkdir -p build

build/aeron-bench-pub: $(OBJECTS) build/pub.o
	$(CC) -o $@ $(filter %.c %.o, $^) $(CFLAGS) $(LDFLAGS)

build/aeron-bench-sub: $(OBJECTS) build/sub.o
	$(CC) -o $@ $(filter %.c %.o, $^) $(CFLAGS) $(LDFLAGS)

build/%.o: src/%.c
	$(CC) -c $< -o $@ $(CFLAGS) $(CFLAGS_EXTRA)

clean:
	@docker image rm aeron-bench:devel 2>/dev/null || true
	@rm -rf build include lib
	@echo "Clean!"

deps: devel-docker-build
	mkdir -p include lib
	docker run --rm -u $$(id -u):$$(id -g) -v $(PWD)/include:/local/include aeron-bench:devel cp -r /usr/local/include/  /local/
	docker run --rm -u $$(id -u):$$(id -g) -v $(PWD)/lib:/local/lib         aeron-bench:devel cp -r /usr/local/lib/      /local/

devel-docker-build:
	docker buildx build --platform=linux/amd64 --target=devel -t aeron-bench:devel .
