# Canvas Engine Makefile
# Modular structure with clean separation between backends
#
# Directory Structure:
#   src/main.c                    - Main entry point (uses interfaces)
#   src/common/types.h            - Common types and interfaces (no external deps)
#   src/renderer/sdl2/            - SDL2 renderer backend
#   src/input/SDL2/               - SDL2 input backend
#   src/sound/SDL2/               - SDL2 sound backend
#   src/jscore/duktape/           - Duktape JS engine backend
#
# To add new backends:
#   - Create src/renderer/opengl/ with renderer_opengl.c/h
#   - Create src/jscore/quickjs/ with jscore_quickjs.c/h
#   - etc.

CC = gcc
CFLAGS = -Wall -c -std=gnu99 -O3 -march=native -flto -DNDEBUG
LDFLAGS = -lm -lSDL2 -lSDL2_image -lSDL2_ttf -lz

# Include paths
INCLUDES = -I. -I/usr/include/SDL2 -D_GNU_SOURCE=1 -D_REENTRANT
INCLUDES += -Iduktape/src -Iduktape/extras -Iduktape/extras/duk-v1-compat -Iduktape/extras/console
INCLUDES += -Isrc

# ============================================================================
# Object Files by Module
# ============================================================================

# Duktape core
DUK_CORE_OBJ = duktape/src/duktape.o \
               duktape/extras/console/duk_console.o \
               duktape/extras/module-node/duk_module_node.o \
               duktape/extras/duk-v1-compat/duk_v1_compat.o

# SDL2 Renderer backend
RENDERER_OBJ = src/renderer/sdl2/renderer_sdl2.o

# SDL2 Input backend
INPUT_OBJ = src/input/SDL2/input_sdl2.o

# SDL2 Sound backend
SOUND_OBJ = src/sound/SDL2/sound_sdl2.o

# Duktape JS core backend
JSCORE_OBJ = src/jscore/duktape/jscore_duk.o

# Main entry point
MAIN_OBJ = src/main.o

# All objects
ALL_OBJ = $(DUK_CORE_OBJ) $(RENDERER_OBJ) $(INPUT_OBJ) $(SOUND_OBJ) $(JSCORE_OBJ) $(MAIN_OBJ)

TARGET = canvas.elf

# Get version from git
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "")
ifneq ($(VERSION),)
CFLAGS += -DTIEWRAP_VERSION='$(VERSION)'
endif

# ============================================================================
# Build Rules
# ============================================================================

all: $(TARGET)

$(TARGET): $(ALL_OBJ)
	$(CC) $(LDFLAGS) $^ -o $@
	@echo "Build complete: $(TARGET)"

# ----------------------------------------------------------------------------
# Duktape Core
# ----------------------------------------------------------------------------
duktape/src/%.o: duktape/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

duktape/extras/console/%.o: duktape/extras/console/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

duktape/extras/module-node/%.o: duktape/extras/module-node/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

duktape/extras/duk-v1-compat/%.o: duktape/extras/duk-v1-compat/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

# ----------------------------------------------------------------------------
# Renderer Backends
# ----------------------------------------------------------------------------
src/renderer/sdl2/%.o: src/renderer/sdl2/%.c src/renderer/sdl2/%.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

# ----------------------------------------------------------------------------
# Input Backends
# ----------------------------------------------------------------------------
src/input/SDL2/%.o: src/input/SDL2/%.c src/input/SDL2/%.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

# ----------------------------------------------------------------------------
# Sound Backends
# ----------------------------------------------------------------------------
src/sound/SDL2/%.o: src/sound/SDL2/%.c src/sound/SDL2/%.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

# ----------------------------------------------------------------------------
# JS Core Backends
# ----------------------------------------------------------------------------
src/jscore/duktape/%.o: src/jscore/duktape/%.c src/jscore/duktape/%.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------
src/main.o: src/main.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

# ============================================================================
# Clean
# ============================================================================

clean:
	rm -f $(ALL_OBJ) $(TARGET)
	find . -name "*.o" -delete
	@echo "Cleaned build artifacts"

# ============================================================================
# Tests
# ============================================================================

test-biolab: $(TARGET)
	./$(TARGET) biolab_impact_test.html

test-ultra: $(TARGET)
	./$(TARGET) ultra.html

test-tapi2: $(TARGET)
	./$(TARGET) tapi2.html

test-ultra-more: $(TARGET)
	./$(TARGET) ultra_more.html

test-all: test-biolab test-ultra test-tapi2 test-ultra-more

# ============================================================================
# Phony Targets
# ============================================================================

.PHONY: all clean test-biolab test-ultra test-tapi2 test-ultra-more test-all

# ============================================================================
# Module Interface Documentation
# ============================================================================
#
# To add a new renderer backend (e.g., OpenGL):
#   1. Create src/renderer/opengl/renderer_opengl.h
#      - Declare: void renderer_opengl_init(RendererInterface* iface);
#   2. Create src/renderer/opengl/renderer_opengl.c
#      - Implement all RendererInterface functions
#      - Implement renderer_opengl_init() to populate function pointers
#   3. Add to Makefile:
#      - RENDERER_OBJ += src/renderer/opengl/renderer_opengl.o
#      - Add build rule
#   4. In main.c, change:
#      - #include "renderer/opengl/renderer_opengl.h"
#      - renderer_opengl_init(&g_renderer);
#
# To add a new JS engine backend (e.g., QuickJS):
#   1. Create src/jscore/quickjs/jscore_quickjs.h
#      - Declare: JSContext* jscore_quickjs_init(void);
#                void jscore_quickjs_register_bindings(...);
#                etc.
#   2. Create src/jscore/quickjs/jscore_quickjs.c
#      - Implement all JS binding functions using QuickJS API
#   3. Add to Makefile:
#      - JSCORE_OBJ = src/jscore/quickjs/jscore_quickjs.o
#      - Add QuickJS library linking
#   4. In main.c, change includes and init calls
#
# The interface definitions are in src/common/types.h
