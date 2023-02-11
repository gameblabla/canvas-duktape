CC := gcc
SDL_INC := $(shell sdl2-config --cflags)
SDL_LIB := $(shell sdl2-config --libs)

CFLAGS := -Wall -c -std=c99 -O2  -I. $(SDL_INC) $(DUKTAPE_INC)
LDFLAGS := -lm $(SDL_LIB) -lduktape
SRCS :=  $(wildcard *.c)
OBJS := $(SRCS:%.c=%.o)
EXE := canvas_example.elf

all: $(OBJS) $(EXE)

$(EXE): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm $(OBJS) && rm $(EXE)
