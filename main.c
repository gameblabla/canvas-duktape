#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL/SDL.h>
#include "duktape.h"

#define WIDTH 640
#define HEIGHT 480

SDL_Surface *window, *canvas;
char name_window[255];

duk_ret_t duk_image_draw(duk_context *ctx);
duk_ret_t duk_image_constructor(duk_context *ctx);
duk_ret_t duk_document_get_element_by_id(duk_context *ctx);
duk_ret_t duk_document_constructor(duk_context *ctx);
duk_ret_t duk_document_getElementById(duk_context *ctx);
duk_ret_t duk_canvas_drawImage(duk_context *ctx);
duk_ret_t duk_canvas_getImageData(duk_context *ctx);
duk_ret_t duk_canvas_putImageData(duk_context *ctx);
duk_ret_t duk_canvas_fillRect(duk_context *ctx);
duk_ret_t duk_canvas_clearRect(duk_context *ctx);
duk_ret_t duk_canvas_getContext(duk_context *ctx);

// Image.draw method
duk_ret_t duk_image_draw(duk_context *ctx) {
    SDL_Surface *image = duk_require_pointer(ctx, 0);
    SDL_Rect rect = {0, 0, image->w, image->h};
    SDL_BlitSurface(image, NULL, window, &rect);
    SDL_Flip(window);
    return 0;
}

// Image constructor
duk_ret_t duk_image_constructor(duk_context *ctx) {
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

duk_ret_t duk_document_get_element_by_id(duk_context *ctx) {
    const char *id = duk_require_string(ctx, 0);
    // Return an empty object for now
    duk_push_object(ctx);
    return 1;
}

duk_ret_t duk_document_constructor(duk_context *ctx) {
    // Create a plain object for the document
    duk_push_object(ctx);
    // Set the 'getElementById' method to the implementation above
    duk_push_c_function(ctx, duk_document_get_element_by_id, 1);
    duk_put_prop_string(ctx, -2, "getElementById");
    return 1;
}

// Document.getElementById method
duk_ret_t duk_document_getElementById(duk_context *ctx) {
    const char *id = duk_require_string(ctx, 0);
    snprintf(name_window, sizeof(name_window), "%s", id);
    duk_push_object(ctx);
    duk_push_c_function(ctx, duk_canvas_getContext, 1);
    duk_put_prop_string(ctx, -2, "getContext");
    return 1;
}

duk_ret_t duk_canvas_drawImage(duk_context *ctx) {
    SDL_Surface *image = duk_require_pointer(ctx, 0);
    int x = duk_require_int(ctx, 1);
    int y = duk_require_int(ctx, 2);

    SDL_Rect dest_rect = { x, y, image->w, image->h };
    SDL_BlitSurface(image, NULL, window, &dest_rect);
    SDL_Flip(window);

    return 0;
}

duk_ret_t duk_canvas_getImageData(duk_context *ctx) {
    SDL_Surface *surface = duk_require_pointer(ctx, 0);
    SDL_Rect rect = {0, 0, surface->w, surface->h};
    SDL_LockSurface(surface);

    SDL_Surface *image = SDL_CreateRGBSurface(0, surface->w, surface->h, 32, 0, 0, 0, 0);
    SDL_BlitSurface(surface, &rect, image, &rect);

    duk_idx_t arr_idx = duk_push_array(ctx);

    for (int y = 0; y < image->h; y++) {
        for (int x = 0; x < image->w; x++) {
            Uint8 *pixel = (Uint8 *) image->pixels + y * image->pitch + x * 4;

            duk_idx_t rgba_idx = duk_push_array(ctx);
            duk_push_int(ctx, pixel[0]);
            duk_put_prop_index(ctx, rgba_idx, 0);
            duk_push_int(ctx, pixel[1]);
            duk_put_prop_index(ctx, rgba_idx, 1);
            duk_push_int(ctx, pixel[2]);
            duk_put_prop_index(ctx, rgba_idx, 2);
            duk_push_int(ctx, pixel[3]);
            duk_put_prop_index(ctx, rgba_idx, 3);

            duk_put_prop_index(ctx, arr_idx, y * image->w + x);
        }
    }

    SDL_UnlockSurface(surface);
    SDL_FreeSurface(image);

    return 1;
}

duk_ret_t duk_canvas_putImageData(duk_context *ctx) {
    // Parse arguments
    SDL_Surface *surface = duk_require_pointer(ctx, 0);
    int x = duk_require_int(ctx, 1);
    int y = duk_require_int(ctx, 2);
    int width = duk_require_int(ctx, 3);
    int height = duk_require_int(ctx, 4);
    const void *data = duk_require_buffer(ctx, 5, NULL);

    // Calculate pitch (bytes per row)
    int bpp = surface->format->BytesPerPixel;
    int pitch = surface->pitch / bpp;

    // Get pointer to pixel data
    uint8_t *pixels = (uint8_t *) surface->pixels + y * pitch * bpp + x * bpp;

    // Copy pixel data from buffer to surface
    int i, j;
    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            memcpy(pixels + j * bpp, data + (i * width + j) * bpp, bpp);
        }
        pixels += pitch * bpp;
    }

    // Return nothing
    return 0;
}

duk_ret_t duk_canvas_fillRect(duk_context *ctx) {
    SDL_Surface *canvas = duk_require_pointer(ctx, 0);
    int x = duk_require_int(ctx, 1);
    int y = duk_require_int(ctx, 2);
    int w = duk_require_int(ctx, 3);
    int h = duk_require_int(ctx, 4);
    Uint32 color = duk_require_uint(ctx, 5);

    SDL_Rect rect = { x, y, w, h };
    SDL_FillRect(canvas, &rect, color);

    return 0;
}

duk_ret_t duk_canvas_clearRect(duk_context *ctx) {
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    int w = duk_require_int(ctx, 2);
    int h = duk_require_int(ctx, 3);

    SDL_Rect rect = { x, y, w, h };
    SDL_FillRect(canvas, &rect, 0x000000);

    return 0;
}

duk_ret_t duk_canvas_getContext(duk_context *ctx) {
    const char *id = duk_require_string(ctx, 0);
    const char *type = duk_require_string(ctx, 1);

    if (strcmp(type, "2d") == 0) {
		// Create a new RGB surface for the canvas
		canvas = SDL_CreateRGBSurface(SDL_HWSURFACE, WIDTH, HEIGHT, 32, 0, 0, 0, 0);
		if (!canvas) {
			duk_error(ctx, DUK_ERR_ERROR, "Failed to create canvas surface: %s", SDL_GetError());
		}

        // Create a plain object for the context
        duk_push_object(ctx);

        // Set the 'canvas' property to the name of the canvas
        duk_push_string(ctx, name_window);
        duk_put_prop_string(ctx, -2, "canvas");

        // Set the 'drawImage' method to draw an image to the canvas
        duk_push_c_function(ctx, duk_canvas_drawImage, 3);
        duk_put_prop_string(ctx, -2, "drawImage");

        // Set the 'getImageData' method to get the pixel data of a rectangle
		duk_push_c_function(ctx, duk_canvas_getImageData, 4);
        duk_put_prop_string(ctx, -2, "getImageData");

        // Set the 'putImageData' method to put the pixel data into a rectangle
        duk_push_c_function(ctx, duk_canvas_putImageData, 3);
        duk_put_prop_string(ctx, -2, "putImageData");

        // Set the 'fillRect' method to fill a rectangle with a color
        duk_push_c_function(ctx, duk_canvas_fillRect, 4);
        duk_put_prop_string(ctx, -2, "fillRect");

        // Set the 'clearRect' method to clear a rectangle to transparent black
        duk_push_c_function(ctx, duk_canvas_clearRect, 4);
        duk_put_prop_string(ctx, -2, "clearRect");

        // Set the 'fillStyle' property to the default fill style (black)
        duk_push_string(ctx, "#000000");
        duk_put_prop_string(ctx, -2, "fillStyle");

        // Set the 'strokeStyle' property to the default stroke style (black)
        duk_push_string(ctx, "#000000");
        duk_put_prop_string(ctx, -2, "strokeStyle");

        // Set the 'lineWidth' property to the default line width (1)
        duk_push_number(ctx, 1);
        duk_put_prop_string(ctx, -2, "lineWidth");

        // Set the 'textAlign' property to the default text alignment (start)
        duk_push_string(ctx, "start");
        duk_put_prop_string(ctx, -2, "textAlign");

        // Set the 'textBaseline' property to the default text baseline (alphabetic)
        duk_push_string(ctx, "alphabetic");
        duk_put_prop_string(ctx, -2, "textBaseline");
        
        // Set the 'getContext' method to return the context object
        duk_push_c_function(ctx, duk_canvas_getContext, 1);
        duk_push_string(ctx, "2d");
        duk_put_prop_string(ctx, -2, "getContext");

        return 1;
    } else {
        duk_type_error(ctx, "Context type not supported: %s", type);
    }

    return 0;
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
	duk_put_global_string(ctx, "getContext");
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, "canvas");

    // Create an instance of the Document object and set the global variable to point to it
    duk_document_constructor(ctx);
    duk_put_global_string(ctx, "document");

    // Load and evaluate the JavaScript code
    if (duk_peval_file(ctx, "canvas.js") != 0) {
        printf("Error: %s\n", duk_safe_to_string(ctx, -1));
        return 1;
    }
    
    // Blit Canvas surface onto main display surface
    SDL_BlitSurface(canvas, NULL, window, NULL);
    // Refresh window
    SDL_Flip(window);

    // Destroy the Duktape heap and quit SDL
    duk_destroy_heap(ctx);
    SDL_Quit();
    return 0;
}

