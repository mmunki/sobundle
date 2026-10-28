CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
CPPFLAGS ?=
LDFLAGS ?=

# Sobundle targets one Android ABI only: arm64-v8a (AArch64).  There is no
# x86 / x86_64 Android target and no such artifact is produced or published.
ANDROID_ABI ?= arm64-v8a
ANDROID_TRIPLE ?= aarch64-linux-android

ifneq ($(ANDROID_ABI),arm64-v8a)
$(error unsupported ANDROID_ABI '$(ANDROID_ABI)': sobundle is arm64-v8a only)
endif

ifneq ($(ANDROID_TRIPLE),aarch64-linux-android)
$(error unsupported ANDROID_TRIPLE '$(ANDROID_TRIPLE)': sobundle is arm64-v8a only)
endif

# The linux-x86_64 component below names the NDK's host toolchain directory,
# not the target ABI.  Everything built for Android is AArch64.
HOST_TARGET := build/host/sobundle
ARM64_DIR := build/lib/$(ANDROID_ABI)
ARM64_LOADER := $(ARM64_DIR)/libloader.so
ARM64_PACKAGER := $(ARM64_DIR)/sobundle

ANDROID_NDK ?= /usr/local/lib/android/sdk/ndk/30.0.16248370
ANDROID_API ?= 24
ANDROID_CC := $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/$(ANDROID_TRIPLE)$(ANDROID_API)-clang
ANDROID_READELF := $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf

ANDROID_CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -fPIC
ANDROID_CPPFLAGS ?= -I.
ANDROID_LDFLAGS ?= -shared -Wl,-soname,libloader.so
ANDROID_LDLIBS ?= -llog -ldl

ANDROID_PACKAGER_CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
ANDROID_PACKAGER_LDFLAGS ?= -pie

.PHONY: all host android arm64 check-arm64 clean test

# The release build is ARM64 only.  The host packager is a build-time tool and
# is not a published asset; use 'make host' when developing on an x86_64 host.
all: android

host: $(HOST_TARGET)

android: $(ARM64_LOADER) $(ARM64_PACKAGER)

arm64: android

check-arm64: android
	@ANDROID_READELF="$(ANDROID_READELF)" bash tests/check-arm64.sh

$(HOST_TARGET): sobundle_pack.c sobundle.h | build/host
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ sobundle_pack.c $(LDFLAGS)

$(ARM64_LOADER): libloader.c sobundle.h | $(ARM64_DIR)
	$(ANDROID_CC) $(ANDROID_CPPFLAGS) $(ANDROID_CFLAGS) -o $@ libloader.c $(ANDROID_LDFLAGS) $(ANDROID_LDLIBS)

$(ARM64_PACKAGER): sobundle_pack.c sobundle.h | $(ARM64_DIR)
	$(ANDROID_CC) $(ANDROID_CPPFLAGS) $(ANDROID_PACKAGER_CFLAGS) -o $@ sobundle_pack.c $(ANDROID_PACKAGER_LDFLAGS)

build/host:
	mkdir -p build/host

$(ARM64_DIR):
	mkdir -p $(ARM64_DIR)

clean:
	rm -rf build

test: host
	@bash tests/run.sh
