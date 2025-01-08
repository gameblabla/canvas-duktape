#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "duktape.h"
#include "duktape/extras/console/duk_console.h"

/* Global Variables */
static SDL_Window* g_window = NULL;
static SDL_Renderer* g_renderer = NULL;
static int g_win_w = 1280, g_win_h = 720;

/* Structures */
typedef struct {
    SDL_Texture* tex;
    int w, h;
    char fname[512];
} MyImage;

typedef struct {
    int active;
    double intervalMs;
    double nextTime;
    int funcId;
} IntervalInfo;

typedef struct {
    int width;
    int height;
} MyCanvas;

/* Constants */
#define MAX_INTERVALS 64

/* Global State */
static IntervalInfo g_intervals[MAX_INTERVALS];
static int g_func_id_gen = 0;
static int g_window_onload_funcId = -1;

/* Utility Functions to Store and Retrieve Functions */
static int store_func(duk_context* ctx, int funcIndex){
    duk_push_heap_stash(ctx);
    duk_get_prop_string(ctx, -1, "g_funcStore");
    if(duk_is_undefined(ctx, -1)){
        duk_pop(ctx);
        duk_push_object(ctx);
        duk_put_prop_string(ctx, -2, "g_funcStore");
        duk_get_prop_string(ctx, -1, "g_funcStore");
    }
    int fid = ++g_func_id_gen;
    duk_push_int(ctx, fid);
    duk_dup(ctx, funcIndex);
    duk_put_prop(ctx, -3);
    duk_pop_2(ctx);
    return fid;
}

static void push_stored_func(duk_context* ctx, int fid){
    duk_push_heap_stash(ctx);
    duk_get_prop_string(ctx, -1, "g_funcStore");
    if(duk_is_undefined(ctx, -1)){
        duk_pop_2(ctx);
        duk_push_undefined(ctx);
        return;
    }
    duk_push_int(ctx, fid);
    duk_get_prop(ctx, -2);
    duk_remove(ctx, -2); // Remove 'g_funcStore'
    duk_remove(ctx, -2); // Remove heap stash
}

/* Image Handling Functions */
static MyImage* get_image_ptr(duk_context* ctx, int idx){
    duk_get_prop_string(ctx, idx, "\xFF""ptr");
    MyImage* p = (MyImage*)duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    return p;
}

static void load_sync(MyImage* img){
    if(img->tex || !img->fname[0]) return;
    SDL_Surface* sf = IMG_Load(img->fname);
    if(!sf){
        fprintf(stderr, "[load_sync] Could not load '%s': %s\n", img->fname, IMG_GetError());
        return;
    }
    img->tex = SDL_CreateTextureFromSurface(g_renderer, sf);
    if(!img->tex){
        fprintf(stderr, "[load_sync] Could not create texture from '%s': %s\n", img->fname, SDL_GetError());
    }
    img->w = sf->w;
    img->h = sf->h;
    SDL_FreeSurface(sf);
}

/* Image Event Listener Handling */
static void call_img_listeners(duk_context* ctx, int obj_idx, const char* evName){
    duk_get_prop_string(ctx, obj_idx, "listeners");
    if(!duk_is_object(ctx, -1)){
        duk_pop(ctx);
        return;
    }
    duk_get_prop_string(ctx, -1, evName);
    if(!duk_is_array(ctx, -1)){
        duk_pop_2(ctx);
        return;
    }
    duk_uarridx_t n = (duk_uarridx_t)duk_get_length(ctx, -1);
    for(duk_uarridx_t i=0; i<n; i++){
        duk_get_prop_index(ctx, -1, i);
        if(duk_is_callable(ctx, -1)){
            duk_dup(ctx, obj_idx); // Push 'this'
            if(duk_pcall_method(ctx, 0)!=0){
                fprintf(stderr, "[call_img_listeners] Error in '%s' callback: %s\n",
                        evName, duk_safe_to_string(ctx, -1));
            }
        }
        duk_pop(ctx); // Pop the function
    }
    duk_pop_2(ctx); // Pop array and listeners object
}

/* Image.addEventListener Implementation */
static duk_ret_t js_img_addEventListener(duk_context* ctx){
    duk_push_this(ctx);
    const char* evName = duk_require_string(ctx, 0);
    duk_require_callable(ctx, 1);

    duk_get_prop_string(ctx, -1, "listeners");
    if(!duk_is_object(ctx, -1)){
        duk_pop(ctx);
        duk_push_object(ctx);
        duk_put_prop_string(ctx, -2, "listeners");
        duk_get_prop_string(ctx, -1, "listeners");
    }
    duk_get_prop_string(ctx, -1, evName);
    if(!duk_is_array(ctx, -1)){
        duk_pop(ctx);
        duk_push_array(ctx);
        duk_put_prop_string(ctx, -2, evName);
        duk_get_prop_string(ctx, -1, evName);
    }
    duk_uarridx_t len = (duk_uarridx_t)duk_get_length(ctx, -1);
    duk_dup(ctx, 1);
    duk_put_prop_index(ctx, -2, len);
    duk_pop_3(ctx); // Pop array, listeners, and 'this'

    return 0;
}

/* Image.src Setter Implementation */
static duk_ret_t js_img_src_setter(duk_context* ctx){
    duk_push_this(ctx);
    MyImage* img = get_image_ptr(ctx, -1);
    if(!img){
        duk_pop(ctx);
        return 0;
    }
    const char* fn = duk_require_string(ctx, 0);
    memset(img->fname, 0, sizeof(img->fname));
    strncpy(img->fname, fn, sizeof(img->fname)-1);

    if(img->tex){
        SDL_DestroyTexture(img->tex);
        img->tex = NULL;
    }
    load_sync(img);

    if(img->tex){
        /* Fire 'load' event */
        call_img_listeners(ctx, -1, "load");
    }
    duk_pop(ctx);
    return 0;
}

/* Image Constructor */
static duk_ret_t ImageCtor(duk_context* ctx){
    duk_push_object(ctx);
    MyImage* im = (MyImage*)calloc(1, sizeof(MyImage));
    duk_push_pointer(ctx, (void*)im);
    duk_put_prop_string(ctx, -2, "\xFF""ptr");

    /* addEventListener */
    duk_push_c_function(ctx, js_img_addEventListener, 2);
    duk_put_prop_string(ctx, -2, "addEventListener");

    /* src property with setter */
    duk_push_string(ctx, "src");
    duk_push_c_function(ctx, js_img_src_setter, 1);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_SETTER | DUK_DEFPROP_ENUMERABLE);

    return 1;
}

/* Create Image Object in Duktape */
static void create_image(duk_context* ctx){
    duk_push_c_function(ctx, ImageCtor, 0);
    duk_put_global_string(ctx, "Image");
}

/* Canvas Property Getters and Setters */
static MyCanvas* get_canvas_ptr(duk_context* ctx, int idx){
    duk_get_prop_string(ctx, idx, "\xFF""canvasPtr");
    MyCanvas* p = (MyCanvas*)duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    return p;
}

static duk_ret_t js_canvas_height_setter(duk_context* ctx){
    duk_push_this(ctx);
    MyCanvas* cinfo = get_canvas_ptr(ctx, -1);
    if(!cinfo){
        duk_pop(ctx);
        return 0;
    }
    int newH = duk_require_int(ctx, 0);
    printf("[js_canvas_height_setter] Setting height to %d\n", newH);
    cinfo->height = newH;

    /* Emulate "clearing" the canvas by clearing the renderer (trick used by a few games) */
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);

    duk_pop(ctx);
    return 0;
}

static duk_ret_t js_canvas_height_getter(duk_context* ctx){
    duk_push_this(ctx);
    MyCanvas* cinfo = get_canvas_ptr(ctx, -1);
    duk_pop(ctx);
    printf("[js_canvas_height_getter] Getting height: %d\n", cinfo->height);
    duk_push_int(ctx, cinfo->height);
    return 1;
}

static duk_ret_t js_canvas_width_setter(duk_context* ctx){
    duk_push_this(ctx);
    MyCanvas* cinfo = get_canvas_ptr(ctx, -1);
    if(!cinfo){
        duk_pop(ctx);
        return 0;
    }
    int newW = duk_require_int(ctx, 0);
    printf("[js_canvas_width_setter] Setting width to %d\n", newW);
    cinfo->width = newW;

    /* Emulate "clearing" the canvas by clearing the renderer */
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);

    duk_pop(ctx);
    return 0;
}

static duk_ret_t js_canvas_width_getter(duk_context* ctx){
    duk_push_this(ctx);
    MyCanvas* cinfo = get_canvas_ptr(ctx, -1);
    duk_pop(ctx);
    printf("[js_canvas_width_getter] Getting width: %d\n", cinfo->width);
    duk_push_int(ctx, cinfo->width);
    return 1;
}

/* Enhanced drawImage Implementation */
static duk_ret_t js_drawImage(duk_context* ctx){
    int nargs = duk_get_top(ctx);
    printf("[js_drawImage] Called with %d arguments.\n", nargs);

    if(nargs !=3 && nargs !=5 && nargs !=9){
        return duk_error(ctx, DUK_ERR_TYPE_ERROR,
                        "drawImage() requires 3, 5, or 9 arguments, got %d", nargs);
    }

    MyImage* im = get_image_ptr(ctx, 0);
    if(!im){
        fprintf(stderr, "[js_drawImage] No image pointer.\n");
        return 0;
    }
    load_sync(im);
    if(!im->tex){
        fprintf(stderr, "[js_drawImage] No texture loaded.\n");
        return 0;
    }

    /* Initialize source and destination rectangles */
    int sx = 0, sy = 0, sw = im->w, sh = im->h;
    int dx = 0, dy = 0, dw = im->w, dh = im->h;

    if(nargs ==3){
        /* drawImage(img, dx, dy) */
        dx = duk_require_int(ctx,1);
        dy = duk_require_int(ctx,2);
        printf("[js_drawImage] 3-arg: dx=%d, dy=%d\n", dx, dy);
    }
    else if(nargs ==5){
        /* drawImage(img, dx, dy, dWidth, dHeight) */
        dx = duk_require_int(ctx,1);
        dy = duk_require_int(ctx,2);
        dw = duk_require_int(ctx,3);
        dh = duk_require_int(ctx,4);
        printf("[js_drawImage] 5-arg: dx=%d, dy=%d, dw=%d, dh=%d\n", dx, dy, dw, dh);
    }
    else if(nargs ==9){
        /* drawImage(img, sx, sy, sWidth, sHeight, dx, dy, dWidth, dHeight) */
        sx = duk_require_int(ctx,1);
        sy = duk_require_int(ctx,2);
        sw = duk_require_int(ctx,3);
        sh = duk_require_int(ctx,4);
        dx = duk_require_int(ctx,5);
        dy = duk_require_int(ctx,6);
        dw = duk_require_int(ctx,7);
        dh = duk_require_int(ctx,8);
        printf("[js_drawImage] 9-arg: sx=%d, sy=%d, sw=%d, sh=%d, dx=%d, dy=%d, dw=%d, dh=%d\n",
               sx, sy, sw, sh, dx, dy, dw, dh);
    }

    /* Validate dimensions */
    if(sw <=0 || sh <=0 || dw <=0 || dh <=0){
        fprintf(stderr, "[js_drawImage] Invalid dimensions: sw=%d, sh=%d, dw=%d, dh=%d\n",
                sw, sh, dw, dh);
        return 0;
    }

    /* Validate source rectangle bounds */
    if(sx <0 || sy <0 || (sx + sw) > im->w || (sy + sh) > im->h){
        fprintf(stderr, "[js_drawImage] Source rectangle out of bounds: sx=%d, sy=%d, sw=%d, sh=%d\n",
                sx, sy, sw, sh);
        return 0;
    }

    /* Define SDL_Rect structures */
    SDL_Rect srcRect = { sx, sy, sw, sh };
    SDL_Rect dstRect = { dx, dy, dw, dh };

    /* Perform the rendering */
    if(SDL_RenderCopy(g_renderer, im->tex, &srcRect, &dstRect) !=0 ){
        fprintf(stderr, "[js_drawImage] SDL_RenderCopy failed: %s\n", SDL_GetError());
    }
    else{
        SDL_RenderPresent(g_renderer);
        printf("[js_drawImage] Rendered successfully.\n");
    }

    return 0;
}

/* getContext Implementation */
static duk_ret_t js_getContext(duk_context* ctx){
    const char* mode = duk_require_string(ctx,0);
    if(strcmp(mode, "2d") !=0 ){
        return duk_error(ctx, DUK_ERR_TYPE_ERROR, "Only '2d' context is supported");
    }
    duk_push_object(ctx);
    duk_push_c_function(ctx, js_drawImage, DUK_VARARGS); // Allow variable args
    duk_put_prop_string(ctx, -2, "drawImage");
    return 1;
}

/* document.getElementById Implementation */
static duk_ret_t js_getElementById(duk_context* ctx){
    const char* id = duk_require_string(ctx,0);
    if(!strcmp(id, "canvas")){
        duk_push_object(ctx);

        /* Allocate and store MyCanvas */
        MyCanvas* c = (MyCanvas*)calloc(1, sizeof(MyCanvas));
        if(!c){
            duk_push_error_object(ctx, DUK_ERR_ERROR, "Failed to allocate MyCanvas");
            return duk_throw(ctx);
        }
        c->width = g_win_w;
        c->height = g_win_h;

        duk_push_pointer(ctx, (void*)c);
        duk_put_prop_string(ctx, -2, "\xFF""canvasPtr");

        /* Define width property with getter/setter */
        duk_push_string(ctx, "width");
        duk_push_c_function(ctx, js_canvas_width_getter, 0);
        duk_push_c_function(ctx, js_canvas_width_setter, 1);
        duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER);

        /* Define height property with getter/setter */
        duk_push_string(ctx, "height");
        duk_push_c_function(ctx, js_canvas_height_getter, 0);
        duk_push_c_function(ctx, js_canvas_height_setter, 1);
        duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER);

        /* Define getContext */
        duk_push_c_function(ctx, js_getContext, 1);
        duk_put_prop_string(ctx, -2, "getContext");

        printf("[js_getElementById] Created canvas object.\n");
        return 1;
    }
    duk_push_undefined(ctx);
    return 1;
}

/* Create Document Object in Duktape */
static void create_document(duk_context* ctx){
    duk_push_object(ctx);
    duk_push_c_function(ctx, js_getElementById, 1);
    duk_put_prop_string(ctx, -2, "getElementById");
    duk_put_global_string(ctx, "document");
}

static double nowMs(void){ return (double)SDL_GetTicks(); }

/* Window Functions: onload, setInterval */
static duk_ret_t js_setInterval(duk_context* ctx){
    duk_require_callable(ctx,0);
    double ms = duk_require_number(ctx,1);
    int fid = store_func(ctx, 0);
    for(int i=0; i<MAX_INTERVALS; i++){
        if(!g_intervals[i].active){
            g_intervals[i].active = 1;
            g_intervals[i].intervalMs = ms;
            g_intervals[i].nextTime = nowMs() + ms;
            g_intervals[i].funcId = fid;
            duk_push_int(ctx, i);
            printf("[js_setInterval] Registered setInterval with fid=%d at index=%d\n", fid, i);
            return 1;
        }
    }
    duk_push_int(ctx, -1);
    fprintf(stderr, "[js_setInterval] No available interval slots.\n");
    return 1;
}

static duk_ret_t js_onload_set(duk_context* ctx){
    duk_require_callable(ctx,0);
    g_window_onload_funcId = store_func(ctx, 0);
    printf("[js_onload_set] Registered window.onload with fid=%d\n", g_window_onload_funcId);
    return 0;
}

static duk_ret_t js_onload_get(duk_context* ctx){
    if(g_window_onload_funcId <0 ){
        duk_push_undefined(ctx);
    }
    else{
        push_stored_func(ctx, g_window_onload_funcId);
    }
    return 1;
}

/* Call window.onload */
static void call_window_onload(duk_context* ctx){
    if(g_window_onload_funcId <0) return;
    push_stored_func(ctx, g_window_onload_funcId);
    if(duk_is_callable(ctx, -1)){
        duk_get_global_string(ctx, "window");
        printf("[call_window_onload] Calling window.onload\n");
        if(duk_pcall_method(ctx,0)!=0){
            fprintf(stderr, "[call_window_onload] Error: %s\n", duk_safe_to_string(ctx, -1));
        }
        duk_pop(ctx);
    }
    else{
        duk_pop(ctx);
    }
}

/* Check and Execute setInterval Callbacks */
static void check_intervals(duk_context* ctx){
    double t = nowMs();
    for(int i=0; i<MAX_INTERVALS; i++){
        if(!g_intervals[i].active) continue;
        if(t >= g_intervals[i].nextTime){
            g_intervals[i].nextTime += g_intervals[i].intervalMs;
            push_stored_func(ctx, g_intervals[i].funcId);
            if(duk_is_callable(ctx, -1)){
                duk_get_global_string(ctx, "window");
                printf("[check_intervals] Executing setInterval callback fid=%d\n", g_intervals[i].funcId);
                if(duk_pcall_method(ctx,0)!=0){
                    fprintf(stderr, "[setInterval] callback error: %s\n", duk_safe_to_string(ctx, -1));
                }
                duk_pop(ctx);
            }
            else{
                duk_pop(ctx);
            }
        }
    }
}

/* Alert Implementation */
static duk_ret_t js_alert(duk_context* ctx){
    const char* msg = duk_require_string(ctx,0);
    printf("[alert] %s\n", msg);
    return 0;
}

/* Create Window Object in Duktape */
static void create_window_obj(duk_context* ctx){
    duk_push_object(ctx);
    duk_push_c_function(ctx, js_setInterval, 2);
    duk_put_prop_string(ctx, -2, "setInterval");

    /* Define onload property with getter and setter */
    duk_push_string(ctx, "onload");
    duk_push_c_function(ctx, js_onload_get, 0);
    duk_push_c_function(ctx, js_onload_set, 1);
    duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER);

    duk_put_global_string(ctx, "window");
}

/* Create Alert Function in Duktape */
static void create_alert_obj(duk_context* ctx){
    duk_push_c_function(ctx, js_alert, 1);
    duk_put_global_string(ctx, "alert");
}

/* Evaluate a JavaScript File */
static duk_int_t eval_file(duk_context* ctx, const char* path){
    FILE* f = fopen(path, "rb");
    if(!f){
        duk_push_error_object(ctx, DUK_ERR_ERROR, "Cannot open file: %s", path);
        return DUK_EXEC_ERROR;
    }
    fseek(f,0, SEEK_END);
    long sz = ftell(f);
    fseek(f,0, SEEK_SET);
    char* buf = (char*)malloc(sz+1);
    if(!buf){
        fclose(f);
        duk_push_error_object(ctx, DUK_ERR_ERROR, "OOM reading file: %s", path);
        return DUK_EXEC_ERROR;
    }
    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    if(rd != (size_t)sz){
        free(buf);
        duk_push_error_object(ctx, DUK_ERR_ERROR, "Bad read from file: %s", path);
        return DUK_EXEC_ERROR;
    }
    buf[sz] = 0;
    duk_int_t rc = duk_peval_string(ctx, buf);
    free(buf);
    return rc;
}

/* MAIN Function */
int main(int argc, char** argv){
    if(argc <2){
        fprintf(stderr, "Usage: %s <script.js>\n", argv[0]);
        return 1;
    }

    /* Initialize SDL */
    if(SDL_Init(SDL_INIT_VIDEO) <0 ){
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }
    if(!(IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) & (IMG_INIT_PNG | IMG_INIT_JPG))){
        fprintf(stderr, "IMG_Init error: %s\n", IMG_GetError());
        SDL_Quit();
        return 1;
    }

    /* Create SDL Window */
    g_window = SDL_CreateWindow("Canvas Demo",
                                SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                g_win_w, g_win_h, 0);
    if(!g_window){
        fprintf(stderr, "Failed SDL_CreateWindow: %s\n", SDL_GetError());
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    /* Create SDL Renderer */
    g_renderer = SDL_CreateRenderer(g_window, -1,
                                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(!g_renderer){
        fprintf(stderr, "Failed SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    /* Clear the screen once at startup */
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderPresent(g_renderer);

    /* Create Duktape Context */
    duk_context* ctx = duk_create_heap_default();
    if(!ctx){
        fprintf(stderr, "Failed to create Duktape heap.\n");
        SDL_DestroyRenderer(g_renderer);
        SDL_DestroyWindow(g_window);
        IMG_Quit();
        SDL_Quit();
        return 1;
    }
    duk_console_init(ctx, DUK_CONSOLE_PROXY_WRAPPER);

    /* Create Mock Browser Objects */
    create_document(ctx);
    create_image(ctx);
    create_window_obj(ctx);
    create_alert_obj(ctx);

    /* Evaluate the User's Script */
    duk_int_t rc = eval_file(ctx, argv[1]);
    if(rc == DUK_EXEC_SUCCESS){
        duk_pop(ctx);
        /* Call window.onload if set */
        call_window_onload(ctx);
    }
    else{
        fprintf(stderr, "Script error: %s\n", duk_safe_to_string(ctx, -1));
        duk_pop(ctx);
    }

    /* Main Loop */
    int running =1;
    while(running){
        SDL_Event e;
        
        while(SDL_PollEvent(&e)){
            if(e.type == SDL_QUIT){
                running =0;
            }
        }

        /* Check and Execute setInterval Callbacks */
        check_intervals(ctx);

        SDL_Delay(10);
    }

    /* Cleanup */
    duk_destroy_heap(ctx);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
