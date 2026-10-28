CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
CPPFLAGS ?=
LDFLAGS ?=

HOST_TARGET := build/sobundle
ANDROID_TARGET := build/lib/arm64-v8a/libloader.so

ANDROID_NDK ?= /usr/local/lib/android/sdk/ndk/30.0.16248370
ANDROID_API ?= 24
ANDROID_TRIPLE ?= aarch64-linux-android
ANDROID_CC := $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/$(ANDROID_TRIPLE)$(ANDROID_API)-clang

ANDROID_CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -fPIC
ANDROID_CPPFLAGS ?= -I.
ANDROID_LDFLAGS ?= -shared -Wl,-soname,libloader.so
ANDROID_LDLIBS ?= -llog -ldl

.PHONY: all host android clean test

all: host android

host: $(HOST_TARGET)

android: $(ANDROID_TARGET)

$(HOST_TARGET): sobundle_pack.c sobundle.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ sobundle_pack.c $(LDFLAGS)

$(ANDROID_TARGET): libloader.c sobundle.h | build/android
	$(ANDROID_CC) $(ANDROID_CPPFLAGS) $(ANDROID_CFLAGS) -o $@ libloader.c $(ANDROID_LDFLAGS) $(ANDROID_LDLIBS)

build:
	mkdir -p build

build/android:
	mkdir -p build/lib/arm64-v8a

clean:
	rm -rf build

test: host
	@bash tests/run.sh
