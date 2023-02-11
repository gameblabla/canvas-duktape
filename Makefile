CC := gcc
SDL_INC := $(shell sdl2-config --cflags)
SDL_LIB := $(shell sdl2-config --libs) -lSDL2_image
DUKTAPE_INC := -Iduktape/src -Iduktape/extras -Iduktape/extras/duk-v1-compat
DUKTAPE_SRC := duktape/src/duktape.c duktape/extras/console/duk_console.c duktape/extras/module-node/duk_module_node.c duktape/extras/duk-v1-compat/duk_v1_compat.c
GIT_DESCRIBE := $(shell git describe --always --dirty)
CFLAGS := -Wall -c -std=c99 -O0 -g3 -DTIEWRAP_VERSION='"$(GIT_DESCRIBE)"' -I. $(SDL_INC) $(DUKTAPE_INC)
LDFLAGS := -lm $(SDL_LIB)
SRCS := $(DUKTAPE_SRC) $(wildcard *.c)
OBJS := $(SRCS:%.c=%.o)
EXE := canvas.elf

all: $(OBJS) $(EXE)

$(EXE): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm $(OBJS) && rm $(EXE)
