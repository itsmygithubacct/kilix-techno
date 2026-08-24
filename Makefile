PROJECT := kilix-techno
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=

CC ?= cc
INSTALL ?= install

CHIPSEQ_DIR := third_party/chip-sequencer
PCMMIX_DIR := third_party/pcm-mixer
SESSION_DIR := third_party/kitty-terminal-session
RASTER_DIR := third_party/soft-raster
STATE_DIR := third_party/kilix-state
KITTY_INPUT_DIR := $(SESSION_DIR)/third_party/kitty-input
KITTY_KEYBOARD_DIR := $(KITTY_INPUT_DIR)/third_party/kitty_keyboard
KITTY_FB_DIR := $(SESSION_DIR)/third_party/kitty-framebuffer

CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
	-Iinclude -I$(CHIPSEQ_DIR)/include -I$(PCMMIX_DIR)/include \
	-I$(SESSION_DIR)/include -I$(KITTY_INPUT_DIR)/include \
	-I$(KITTY_KEYBOARD_DIR)/include -I$(KITTY_FB_DIR)/include \
	-I$(RASTER_DIR)/include -I$(STATE_DIR)/include
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Werror
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -pthread $(WARNINGS)
DEPFLAGS := -MMD -MP
LDLIBS := -lm -lz -pthread

DEPENDENCY_LIBS := \
	$(CHIPSEQ_DIR)/$(BUILD_DIR)/libchip-sequencer.a \
	$(PCMMIX_DIR)/$(BUILD_DIR)/libpcm-mixer.a \
	$(SESSION_DIR)/$(BUILD_DIR)/libkitty-terminal-session.a \
	$(RASTER_DIR)/$(BUILD_DIR)/libsoft-raster.a \
	$(STATE_DIR)/$(BUILD_DIR)/libkilix-state.a

CORE_SOURCES := src/project.c src/compile.c src/actions.c src/settings.c \
	src/dsp.c src/export.c src/audio.c src/ui.c src/cell_ui.c
CORE_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(CORE_SOURCES))
CORE_DEPS := $(CORE_OBJECTS:.o=.d)
TEST_BIN := $(BUILD_DIR)/test-core
THREAD_TEST_BIN := $(BUILD_DIR)/test-thread
FUZZ_BIN := $(BUILD_DIR)/fuzz-project
BENCHMARK_BIN := $(BUILD_DIR)/benchmark
APP_OBJECTS := $(BUILD_DIR)/app.o $(BUILD_DIR)/main.o
APP_BIN := $(BUILD_DIR)/kilix-techno
FUZZ_ITERS ?= 5000

.PHONY: all benchmark clean clean-room-check dependencies fuzz install \
	sanitize test tsanitize

all: $(APP_BIN)

dependencies:
	$(MAKE) -C $(CHIPSEQ_DIR) all
	$(MAKE) -C $(PCMMIX_DIR) all
	$(MAKE) -C $(SESSION_DIR) all
	$(MAKE) -C $(RASTER_DIR) all
	$(MAKE) -C $(STATE_DIR) all

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/lib$(PROJECT).a: $(CORE_OBJECTS)
	ar rcs $@ $^

$(APP_BIN): $(APP_OBJECTS) $(BUILD_DIR)/lib$(PROJECT).a dependencies
	$(CC) $(CFLAGS) $(APP_OBJECTS) $(BUILD_DIR)/lib$(PROJECT).a \
		$(DEPENDENCY_LIBS) $(LDFLAGS) $(LDLIBS) -o $@

$(TEST_BIN): tests/test_core.c $(BUILD_DIR)/lib$(PROJECT).a dependencies
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD_DIR)/lib$(PROJECT).a \
		$(DEPENDENCY_LIBS) $(LDFLAGS) $(LDLIBS) -o $@

$(THREAD_TEST_BIN): tests/test_thread.c $(BUILD_DIR)/lib$(PROJECT).a dependencies
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD_DIR)/lib$(PROJECT).a \
		$(DEPENDENCY_LIBS) $(LDFLAGS) $(LDLIBS) -o $@

$(FUZZ_BIN): tests/fuzz_project.c $(BUILD_DIR)/lib$(PROJECT).a dependencies
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD_DIR)/lib$(PROJECT).a \
		$(DEPENDENCY_LIBS) $(LDFLAGS) $(LDLIBS) -o $@

$(BENCHMARK_BIN): benchmarks/benchmark.c $(BUILD_DIR)/lib$(PROJECT).a dependencies
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD_DIR)/lib$(PROJECT).a \
		$(DEPENDENCY_LIBS) $(LDFLAGS) $(LDLIBS) -o $@

test: $(TEST_BIN) $(THREAD_TEST_BIN) $(APP_BIN)
	$(TEST_BIN)
	$(THREAD_TEST_BIN)
	sh tests/test_cli.sh $(APP_BIN)
	sh tests/test_terminal.sh $(APP_BIN)

sanitize:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/sanitize \
		CFLAGS='-O1 -g3 -fsanitize=address,undefined \
		-fno-sanitize-recover=undefined -fno-omit-frame-pointer' \
		LDFLAGS='-fsanitize=address,undefined' test

tsanitize:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/tsanitize \
		CFLAGS='-O1 -g3 -fsanitize=thread -fno-omit-frame-pointer' \
		LDFLAGS='-fsanitize=thread' $(BUILD_DIR)/tsanitize/test-thread
	$(BUILD_DIR)/tsanitize/test-thread

fuzz: $(FUZZ_BIN)
	$(FUZZ_BIN) $(FUZZ_ITERS)

benchmark: $(BENCHMARK_BIN)
	$(BENCHMARK_BIN)

clean-room-check:
	./tools/clean-room-check.sh

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin \
		$(DESTDIR)$(PREFIX)/share/licenses/$(PROJECT) \
		$(DESTDIR)$(PREFIX)/share/doc/$(PROJECT)
	$(INSTALL) -m 0755 $(APP_BIN) $(DESTDIR)$(PREFIX)/bin/$(PROJECT)
	$(INSTALL) -m 0644 LICENSE THIRD_PARTY.md \
		$(DESTDIR)$(PREFIX)/share/licenses/$(PROJECT)/
	$(INSTALL) -m 0644 README.md docs/ARCHITECTURE.md \
		docs/PROJECT-FORMAT.md docs/PROVENANCE.md \
		$(DESTDIR)$(PREFIX)/share/doc/$(PROJECT)/

clean:
	rm -rf $(BUILD_DIR)

-include $(CORE_DEPS)
