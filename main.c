```c
#include <stdio.h>
#include <SDL2/SDL.h>
#include "duktape.h"

int sdl_fillRect(duk_context *ctx) {
	int x = duk_to_int(ctx, 0);
	int y = duk_to_int(ctx, 1);
	int w = duk_to_int(ctx, 2);
	int h = duk_to_int(ctx, 3);
	SDL_Rect rect = {x, y, w, h};
	SDL_RenderDrawRect(SDL_GetRenderer(SDL_GetWindowFromID(1)), &rect);
	return 0;
}

int sdl_setFillStyle(duk_context *ctx) {
	const char *color = duk_to_string(ctx, 0);
	int r, g, b;
	sscanf(color, "#%2x%2x%2x", &r, &g, &b);
	SDL_SetRenderDrawColor(SDL_GetRenderer(SDL_GetWindowFromID(1)), r, g, b, 255);
	return 0;
}

int main(int argc, char *argv[]) {
	duk_context *ctx = duk_create_heap_default();
	duk_eval_string(ctx, "var canvas = {}; canvas.fillRect = function(x, y, w, h) { native.fillRect(x, y, w, h); }; canvas.fillStyle = function(color) { native.setFillStyle(color); };");
	duk_push_global_object(ctx);
	duk_push_object(ctx);
	duk_push_c_function(ctx, sdl_fillRect, 4);
	duk_put_prop_string(ctx, -2, "fillRect");
	duk_push_c_function(ctx, sdl_setFillStyle, 1);
	duk_put_prop_string(ctx, -2, "setFillStyle");
	duk_put_prop_string(ctx, -2, "native");

	SDL_Init(SDL_INIT_VIDEO);
	SDL_Window *window = SDL_CreateWindow("Canvas", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 600, 0);
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);

	duk_eval_string(ctx, "canvas.fillStyle('#ff0000'); canvas.fillRect(100, 100, 200, 200);");

	SDL_RenderPresent(renderer);
  
	// Wait for a key press
	SDL_Event event;
	while (SDL_WaitEvent(&event) && event.type != SDL_QUIT) {
	}

	duk_destroy_heap(ctx);
	SDL_Quit();

	return 0;
}
```
