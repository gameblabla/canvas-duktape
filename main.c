#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL/SDL.h>
#include "duktape.h"

#define WIDTH 640
#define HEIGHT 480

SDL_Surface *window;

// Image.draw method
static duk_ret_t duk_image_draw(duk_context *ctx) {
    SDL_Surface *image = duk_require_pointer(ctx, 0);
    SDL_Rect rect = {0, 0, image->w, image->h};
    SDL_BlitSurface(image, NULL, window, &rect);
    SDL_Flip(window);
    return 0;
}

// Image constructor
static duk_ret_t duk_image_constructor(duk_context *ctx) {
    SDL_Surface *image;
    const char *filename = duk_require_string(ctx, 0);

    // Load the BMP file
    image = SDL_LoadBMP(filename);
    if (!image) {
        duk_type_error(ctx, "Failed to load image: %s", SDL_GetError());
    }

    // Get the canvas element and its 2D context
    duk_get_global_string(ctx, "document");
    duk_get_prop_string(ctx, -1, "getElementById");
    duk_push_string(ctx, "canvas");
    duk_call(ctx, 1);
    duk_get_prop_string(ctx, -1, "getContext");
    duk_push_string(ctx, "2d");
    duk_call(ctx, 1);
    duk_put_global_string(ctx, "context");

    // Create a plain object for the image
    duk_push_object(ctx);

    // Set the 'src' property to the filename
    duk_push_string(ctx, filename);
    duk_put_prop_string(ctx, -2, "src");

    // Set the 'draw' method to draw the image to the canvas
    duk_push_c_function(ctx, duk_image_draw, 1);
    duk_put_prop_string(ctx, -2, "draw");

    // Set the 'surface' property to the SDL_Surface pointer
    duk_push_pointer(ctx, image);
    duk_put_prop_string(ctx, -2, "surface");

    return 1;
}


static duk_ret_t duk_document_get_element_by_id(duk_context *ctx) {
    const char *id = duk_require_string(ctx, 0);
    // Return an empty object for now
    duk_push_object(ctx);
    return 1;
}

static duk_ret_t duk_document_constructor(duk_context *ctx) {
    // Create a plain object for the document
    duk_push_object(ctx);
    // Set the 'getElementById' method to the implementation above
    duk_push_c_function(ctx, duk_document_get_element_by_id, 1);
    duk_put_prop_string(ctx, -2, "getElementById");
    return 1;
}

// Document.getElementById method
static duk_ret_t duk_document_getElementById(duk_context *ctx) {
    const char *id = duk_require_string(ctx, 0);
    duk_push_undefined(ctx);  // dummy return value
    return 1;
}

// Canvas.getContext method
static duk_ret_t duk_canvas_getContext(duk_context *ctx) {
    const char *type = duk_require_string(ctx, 0);
    duk_push_undefined(ctx);  // dummy return value
    return 1;
}


int main(int argc, char *argv[]) {
    // Initialize SDL 1.2
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("Failed to initialize SDL 1.2: %s\n", SDL_GetError());
        return 1;
    }

    // Create the window
    window = SDL_SetVideoMode(WIDTH, HEIGHT, 32, SDL_HWSURFACE | SDL_DOUBLEBUF);
    if (!window) {
        printf("Failed to create window: %s\n", SDL_GetError());
        return 1;
    }

    // Initialize Duktape
    duk_context *ctx = duk_create_heap_default();
    if (!ctx) {
        printf("Failed to create Duktape heap\n");
        return 1;
    }

    // Register the Image constructor
    duk_push_c_function(ctx, duk_image_constructor, 0);
    duk_put_global_string(ctx, "Image");

    // Set up the 'document' object
    duk_push_object(ctx);

    // Set up the 'getElementById' function
    duk_push_c_function(ctx, duk_document_getElementById, 1);
    duk_put_prop_string(ctx, -2, "getElementById");

    // Set up the 'canvas' object
    duk_push_object(ctx);

    // Set the 'id' property to 'canvas'
    duk_push_string(ctx, "canvas");
    duk_put_prop_string(ctx, -2, "id");

    // Set the 'getContext' method to return a plain object
    duk_push_c_function(ctx, duk_canvas_getContext, 1);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, "canvas");

    // Put the 'canvas' object into the 'document'
    duk_put_prop_string(ctx, -2, "canvas");
    duk_put_global_string(ctx, "document");

    // Load and evaluate the JavaScript code
    if (duk_peval_file(ctx, "canvas.js") != 0) {
        printf("Error: %s\n", duk_safe_to_string(ctx, -1));
        return 1;
    }

    // Destroy the Duktape heap and quit SDL
    duk_destroy_heap(ctx);
    SDL_Quit();
    return 0;
}

