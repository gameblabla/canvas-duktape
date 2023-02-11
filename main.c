
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <duktape.h>
#include <duk_v1_compat.h>

SDL_Renderer* renderer;

// Load an image into a texture using SDL_Image
SDL_Texture* loadTexture(const char* file, SDL_Renderer* renderer) {
    SDL_Surface* surface = IMG_Load(file);
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

// Implementation of the drawImage function for the canvas
duk_ret_t canvas_drawImage(duk_context *ctx) {
    // Get the arguments
    const char* file = duk_get_string(ctx, 0);
    int x = duk_get_int(ctx, 1);
    int y = duk_get_int(ctx, 2);

    // Load the image
    SDL_Texture* texture = loadTexture(file, renderer);

    // Render the image to the screen
    SDL_Rect dest = {x, y, 0, 0};
    SDL_QueryTexture(texture, NULL, NULL, &dest.w, &dest.h);
    SDL_RenderCopy(renderer, texture, NULL, &dest);

    // Clean up
    SDL_DestroyTexture(texture);

    return 0;
}

duk_ret_t canvas_fillRect(duk_context *ctx) {
	int x = duk_to_int(ctx, 0);
	int y = duk_to_int(ctx, 1);
	int w = duk_to_int(ctx, 2);
	int h = duk_to_int(ctx, 3);
	SDL_Rect rect = {x, y, w, h};
	SDL_RenderFillRect(renderer, &rect);
	return 0;
}

duk_ret_t canvas_setFillStyle(duk_context *ctx) {
#if 0
    // Check if this is a direct property assignment
    if (duk_is_string(ctx, 0)) {
        const char *fill_style = duk_to_string(ctx, 0);
        int r, g, b;
        sscanf(fill_style, "#%2x%2x%2x", &r, &g, &b);
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        return 0;
    }
    // If not, assume this is a function call
    else {
        duk_dup(ctx, 0);
        duk_call(ctx, 0);
        return 1;
    }
#else
	const char *fill_style = duk_to_string(ctx, 0);
    int r, g, b;
    sscanf(fill_style, "#%2x%2x%2x", &r, &g, &b);
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    return 0;
#endif
}


int main(int argc, char* argv[]) {
    // Initialize SDL
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow("Canvas in SDL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    // Initialize Duktape
    duk_context *ctx = duk_create_heap_default();

    // Create the native object
    duk_push_object(ctx);

    // FillRect, setFillStyle, fillStyle
    duk_push_c_function(ctx, canvas_fillRect, 4);
    duk_put_prop_string(ctx, -2, "fillRect");
    duk_push_c_function(ctx, canvas_setFillStyle, 1);
    duk_put_prop_string(ctx, -2, "setFillStyle");
    duk_push_c_function(ctx, canvas_setFillStyle, 1);
    duk_put_prop_string(ctx, -2, "fillStyle");

    // Add the native object to the global context
    duk_put_global_string(ctx, "native");

    // Push the drawImage function onto the Duktape context
    duk_push_c_function(ctx, canvas_drawImage, 3 /*nargs*/);
    duk_put_global_string(ctx, "drawImage");

    // Load the JavaScript code that uses the canvas
    duk_eval_string(ctx, "var canvas = {}; canvas.drawImage = drawImage; canvas.fillRect = function(x, y, w, h) { native.fillRect(x, y, w, h); }; canvas.setFillStyle = function(color) { native.setFillStyle(color); };");
    duk_eval_string(ctx, "Object.defineProperty(canvas, 'fillStyle', { set: function(color) { native.fillStyle(color); } });");
    
    duk_eval_file(ctx, "canvas.js");
    SDL_RenderPresent(renderer);

    // Wait for a key press
    SDL_Event event;
    while (SDL_WaitEvent(&event) && event.type != SDL_QUIT) {
    }

    // Clean up
    duk_destroy_heap(ctx);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

