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
# Feature Detection
# ============================================================================
# Check for realpath and getcwd availability
HAVE_REALPATH := $(shell printf '#include <stdlib.h>\nint main(void) { char* p = realpath("/tmp", NULL); (void)p; return 0; }\n' | $(CC) -o /dev/null -x c - 2>/dev/null && echo 1 || echo 0)
HAVE_GETCWD := $(shell printf '#include <unistd.h>\nint main(void) { char buf[256]; getcwd(buf, 256); return 0; }\n' | $(CC) -o /dev/null -x c - 2>/dev/null && echo 1 || echo 0)

ifeq ($(HAVE_REALPATH),1)
CFLAGS += -DHAVE_REALPATH
endif
ifeq ($(HAVE_GETCWD),1)
CFLAGS += -DHAVE_GETCWD
endif

# ============================================================================
# Duktape Download and Setup
# ============================================================================
# Duktape version to download
DUKTAPE_VERSION = 2.7.0
DUKTAPE_URL = https://github.com/svaarala/duktape/releases/download/v$(DUKTAPE_VERSION)/duktape-$(DUKTAPE_VERSION).tar.xz
DUKTAPE_TAR = duktape-$(DUKTAPE_VERSION).tar.xz
DUKTAPE_SRC = duktape/src

# Download and extract duktape if not present
ifeq ($(wildcard $(DUKTAPE_SRC)),)
$(info Duktape not found. Downloading...)
$(shell wget -q $(DUKTAPE_URL) -O $(DUKTAPE_TAR) && tar xf $(DUKTAPE_TAR) && mv duktape-$(DUKTAPE_VERSION) duktape && rm $(DUKTAPE_TAR))
endif

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
# Duktape Management
# ============================================================================

# Download and extract duktape
duktape: duktape/src
	@echo "Duktape is already present"

duktape/src:
	@echo "Downloading Duktape $(DUKTAPE_VERSION)..."
	@wget -q $(DUKTAPE_URL) -O $(DUKTAPE_TAR)
	@tar xf $(DUKTAPE_TAR)
	@mv duktape-$(DUKTAPE_VERSION) duktape
	@rm $(DUKTAPE_TAR)
	@echo "Duktape downloaded and extracted successfully"

# Remove downloaded duktape
clean-duktape:
	rm -rf duktape
	@echo "Duktape directory removed"

# ============================================================================
# Tests
# ============================================================================

# Test output directory
TEST_OUTPUT_DIR = test_results
$(shell mkdir -p $(TEST_OUTPUT_DIR))

# Run a test from the testsuite folder, capturing console output
# Usage: $(call run_test,test_file.html,output_file.log)
define run_test
	@echo "Running test: $(1)"
	@(cd testsuite && timeout 60 ../$(TARGET) $(1) 2>&1) | tee $(TEST_OUTPUT_DIR)/$(2)
endef

test-biolab: $(TARGET)
	$(call run_test,biolab_impact_test.html,biolab.log)

test-ultra: $(TARGET)
	$(call run_test,ultra.html,ultra.log)

test-tapi2: $(TARGET)
	$(call run_test,tapi2.html,tapi2.log)

test-ultra-more: $(TARGET)
	$(call run_test,ultra_more.html,ultra_more.log)

test-api: $(TARGET)
	$(call run_test,test_api.html,test_api.log)

test-alpha: $(TARGET)
	$(call run_test,testalpha.html,testalpha.log)

test-all: $(TARGET)
	@mkdir -p $(TEST_OUTPUT_DIR)
	@echo "========================================="
	@echo "Running all tests from testsuite/"
	@echo "========================================="
	@for test in testsuite/*.html; do \
		testname=$$(basename $$test); \
		logname=$${testname%.html}.log; \
		echo ""; \
		echo "-----------------------------------------"; \
		echo "Running: $$testname"; \
		echo "-----------------------------------------"; \
		(cd testsuite && timeout 60 ../$(TARGET) $$testname 2>&1) | tee $(TEST_OUTPUT_DIR)/$$logname || true; \
	done
	@echo ""
	@echo "========================================="
	@echo "Test run complete. Logs in $(TEST_OUTPUT_DIR)/"
	@echo "========================================="

# Regression check: compare test output against baseline
# Usage: make check-regressions [BASELINE_DIR=test_results_baseline]
BASELINE_DIR ?= test_results_baseline
check-regressions: $(TEST_OUTPUT_DIR)
	@echo "========================================="
	@echo "Checking for regressions..."
	@echo "Comparing $(TEST_OUTPUT_DIR)/ against $(BASELINE_DIR)/"
	@echo "========================================="
	@regressions=0; \
	for log in $(TEST_OUTPUT_DIR)/*.log; do \
		logname=$$(basename $$log); \
		baseline=$(BASELINE_DIR)/$$logname; \
		if [ -f "$$baseline" ]; then \
			if diff -q "$$log" "$$baseline" > /dev/null 2>&1; then \
				echo "[PASS] $$logname"; \
			else \
				echo "[FAIL] $$logname - differences found:"; \
				diff "$$log" "$$baseline" | head -20; \
				regressions=$$((regressions + 1)); \
			fi; \
		else \
			echo "[WARN] $$logname - no baseline found at $$baseline"; \
		fi; \
	done; \
	echo ""; \
	echo "========================================="; \
	if [ $$regressions -gt 0 ]; then \
		echo "REGRESSIONS DETECTED: $$regressions test(s) failed"; \
		exit 1; \
	else \
		echo "NO REGRESSIONS: All tests passed"; \
	fi

# Save current test results as baseline for future regression checks
save-baseline: $(TEST_OUTPUT_DIR)
	@echo "Saving baseline to $(BASELINE_DIR)/"
	@mkdir -p $(BASELINE_DIR)
	@cp $(TEST_OUTPUT_DIR)/*.log $(BASELINE_DIR)/ 2>/dev/null || echo "No test results to save"
	@echo "Baseline saved"

# Run tests from testsuite directory directly (for manual testing)
test-run: $(TARGET)
	@echo "Running tests from testsuite/ directory..."
	@(cd testsuite && ../$(TARGET))

.PHONY: all clean duktape clean-duktape test-biolab test-ultra test-tapi2 test-ultra-more test-api test-alpha test-all check-regressions save-baseline test-run

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
