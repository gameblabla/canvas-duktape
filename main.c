#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "duktape.h"
#include "duktape/extras/console/duk_console.h"

static SDL_Window* g_window = NULL;
static SDL_Renderer* g_renderer = NULL;
static int g_win_w = 1280, g_win_h = 720;

typedef struct {
    SDL_Texture* tex;
    int w, h;
    char fname[512];
} MyImage;

static MyImage* get_image_ptr(duk_context* ctx, int idx) {
    duk_get_prop_string(ctx, idx, "\xFF""ptr");
    MyImage* p = (MyImage*)duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    return p;
}

static void load_sync(MyImage* img) {
    if (img->tex || !img->fname[0]) return;
    SDL_Surface* sf = IMG_Load(img->fname);
    if (!sf) {
        fprintf(stderr, "[load_sync] Could not load '%s'\n", img->fname);
        return;
    }
    img->tex = SDL_CreateTextureFromSurface(g_renderer, sf);
    img->w   = sf->w;
    img->h   = sf->h;
    SDL_FreeSurface(sf);
}

/* ---------------------------
   AddEventListener for MyImage
   --------------------------- */
static void call_img_listeners(duk_context* ctx, int obj_idx, const char* evName) {
    duk_get_prop_string(ctx, obj_idx, "listeners");
    if (!duk_is_object(ctx, -1)) {
        duk_pop(ctx);
        return;
    }
    duk_get_prop_string(ctx, -1, evName);
    if (!duk_is_array(ctx, -1)) {
        duk_pop_2(ctx);
        return;
    }
    duk_uarridx_t n = (duk_uarridx_t)duk_get_length(ctx, -1);
    for (duk_uarridx_t i=0; i<n; i++){
        duk_get_prop_index(ctx, -1, i);
        if (duk_is_callable(ctx, -1)){
            duk_dup(ctx, obj_idx);
            if (duk_pcall_method(ctx,0)!=0){
                fprintf(stderr,"[call_img_listeners] Error in '%s' callback: %s\n",
                        evName, duk_safe_to_string(ctx,-1));
            }
        }
        duk_pop(ctx);
    }
    duk_pop_2(ctx);
}

static duk_ret_t js_img_addEventListener(duk_context* ctx) {
    duk_push_this(ctx);
    const char* evName = duk_require_string(ctx, 0);
    duk_require_callable(ctx, 1);

    duk_get_prop_string(ctx, -1, "listeners");
    if (!duk_is_object(ctx, -1)){
        duk_pop(ctx);
        duk_push_object(ctx);
        duk_put_prop_string(ctx, -2, "listeners");
        duk_get_prop_string(ctx, -1, "listeners");
    }
    duk_get_prop_string(ctx, -1, evName);
    if (!duk_is_array(ctx, -1)){
        duk_pop(ctx);
        duk_push_array(ctx);
        duk_put_prop_string(ctx, -2, evName);
        duk_get_prop_string(ctx, -1, evName);
    }
    duk_uarridx_t len = (duk_uarridx_t)duk_get_length(ctx, -1);
    duk_dup(ctx, 1);
    duk_put_prop_index(ctx, -2, len);
    duk_pop_3(ctx);
    return 0;
}

/* image.src = "..." setter */
static duk_ret_t js_img_src_setter(duk_context* ctx){
    duk_push_this(ctx);
    MyImage* img = get_image_ptr(ctx, -1);
    if (!img){
        duk_pop(ctx);
        return 0;
    }
    const char* fn = duk_require_string(ctx, 0);
    memset(img->fname,0,sizeof(img->fname));
    strncpy(img->fname, fn, sizeof(img->fname)-1);

    if(img->tex){
        SDL_DestroyTexture(img->tex);
        img->tex=NULL;
    }
    load_sync(img);

    if(img->tex){
        /* Fire 'load' event */
        call_img_listeners(ctx, -1, "load");
    }
    duk_pop(ctx);
    return 0;
}

/* new Image() */
static duk_ret_t ImageCtor(duk_context* ctx){
    duk_push_object(ctx);
    MyImage* i = (MyImage*)calloc(1,sizeof(MyImage));
    duk_push_pointer(ctx,(void*)i);
    duk_put_prop_string(ctx, -2, "\xFF""ptr");

    duk_push_c_function(ctx, js_img_addEventListener, 2);
    duk_put_prop_string(ctx, -2, "addEventListener");

    duk_push_string(ctx, "src");
    duk_push_c_function(ctx, js_img_src_setter, 1);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_SETTER | DUK_DEFPROP_ENUMERABLE);

    return 1;
}

static void create_image(duk_context* ctx){
    duk_push_c_function(ctx, ImageCtor, 0);
    duk_put_global_string(ctx, "Image");
}

/* ---------------------------
   Canvas 2D context
   --------------------------- */
static duk_ret_t js_drawImage(duk_context* ctx) {
    MyImage* im = get_image_ptr(ctx, 0);
    if(!im){
        fprintf(stderr, "[drawImage] No image ptr\n");
        return 0;
    }
    load_sync(im);
    if(!im->tex){
        fprintf(stderr, "[drawImage] No texture loaded\n");
        return 0;
    }
    int x=duk_require_int(ctx,1);
    int y=duk_require_int(ctx,2);
    SDL_Rect dst={ x, y, im->w, im->h };
    SDL_RenderCopy(g_renderer, im->tex, NULL, &dst);
    SDL_RenderPresent(g_renderer);
    return 0;
}

static duk_ret_t js_getContext(duk_context* ctx){
    const char* mode = duk_require_string(ctx, 0);
    if(strcmp(mode,"2d")){
        return duk_error(ctx, DUK_ERR_TYPE_ERROR, "Only '2d' supported");
    }
    duk_push_object(ctx);
    duk_push_c_function(ctx, js_drawImage, 3);
    duk_put_prop_string(ctx, -2, "drawImage");
    return 1;
}

/* document.getElementById("canvas") */
static duk_ret_t js_getElementById(duk_context* ctx){
    const char* id=duk_require_string(ctx,0);
    if(!strcmp(id,"canvas")){
        duk_push_object(ctx);
        duk_push_int(ctx, g_win_w);
        duk_put_prop_string(ctx, -2, "width");
        duk_push_int(ctx, g_win_h);
        duk_put_prop_string(ctx, -2, "height");
        duk_push_c_function(ctx, js_getContext, 1);
        duk_put_prop_string(ctx, -2, "getContext");
        /* If user sets first_layer.height = first_layer.height to 'clear', we do nothing special. */
        return 1;
    }
    duk_push_undefined(ctx);
    return 1;
}

static void create_document(duk_context* ctx){
    duk_push_object(ctx);
    duk_push_c_function(ctx, js_getElementById, 1);
    duk_put_prop_string(ctx, -2, "getElementById");
    duk_put_global_string(ctx, "document");
}

/* ---------------------------
   "window" object:
    - onload
    - setInterval
----------------------------*/
#define MAX_INTERVALS 64
typedef struct {
    int active;
    double intervalMs;
    double nextTime;
    int funcId;
} IntervalInfo;
static IntervalInfo g_intervals[MAX_INTERVALS];
static int g_func_id_gen=0;
static int g_window_onload_id=-1;

static double nowMs(void){ return (double)SDL_GetTicks(); }

static int store_func(duk_context* ctx, int funcIndex){
    duk_push_heap_stash(ctx);
    duk_get_prop_string(ctx, -1, "g_funcStore");
    if(duk_is_undefined(ctx, -1)){
        duk_pop(ctx);
        duk_push_object(ctx);
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, -3, "g_funcStore");
    }
    int fid=++g_func_id_gen;
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
    duk_remove(ctx, -2);
    duk_remove(ctx, -2);
}

static duk_ret_t js_setInterval(duk_context* ctx){
    duk_require_callable(ctx, 0);
    double ms=duk_require_number(ctx,1);
    int fid=store_func(ctx, 0);
    for(int i=0;i<MAX_INTERVALS;i++){
        if(!g_intervals[i].active){
            g_intervals[i].active=1;
            g_intervals[i].intervalMs=ms;
            g_intervals[i].nextTime=nowMs()+ms;
            g_intervals[i].funcId=fid;
            duk_push_int(ctx, i);
            return 1;
        }
    }
    duk_push_int(ctx, -1);
    return 1;
}

static duk_ret_t js_onload_set(duk_context* ctx){
    duk_require_callable(ctx,0);
    g_window_onload_id=store_func(ctx, 0);
    return 0;
}

static duk_ret_t js_onload_get(duk_context* ctx){
    if(g_window_onload_id<0){
        duk_push_undefined(ctx);
    } else {
        push_stored_func(ctx, g_window_onload_id);
    }
    return 1;
}

static void call_window_onload(duk_context* ctx){
    if(g_window_onload_id<0)return;
    push_stored_func(ctx, g_window_onload_id);
    if(duk_is_callable(ctx, -1)){
        duk_get_global_string(ctx, "window");
        if(duk_pcall_method(ctx,0)!=0){
            fprintf(stderr,"[onload] error: %s\n", duk_safe_to_string(ctx,-1));
        }
        duk_pop(ctx);
    } else {
        duk_pop(ctx);
    }
}

static void check_intervals(duk_context* ctx){
    double t=nowMs();
    for(int i=0;i<MAX_INTERVALS;i++){
        if(!g_intervals[i].active) continue;
        if(t>=g_intervals[i].nextTime){
            g_intervals[i].nextTime+=g_intervals[i].intervalMs;
            push_stored_func(ctx, g_intervals[i].funcId);
            if(duk_is_callable(ctx, -1)){
                duk_get_global_string(ctx, "window");
                if(duk_pcall_method(ctx,0)!=0){
                    fprintf(stderr,"[setInterval] error: %s\n",
                            duk_safe_to_string(ctx,-1));
                }
                duk_pop(ctx);
            } else {
                duk_pop(ctx);
            }
        }
    }
}

static duk_ret_t js_alert(duk_context* ctx){
    const char* msg=duk_require_string(ctx,0);
    printf("[alert] %s\n", msg);
    return 0;
}

static void create_window(duk_context* ctx){
    duk_push_object(ctx);
    duk_push_c_function(ctx, js_setInterval, 2);
    duk_put_prop_string(ctx, -2, "setInterval");

    /* onload property */
    duk_push_string(ctx, "onload");
    duk_push_c_function(ctx, js_onload_get, 0);
    duk_push_c_function(ctx, js_onload_set, 1);
    duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER);

    duk_put_global_string(ctx, "window");
}

/* optional global alert() */
static void create_alert(duk_context* ctx){
    duk_push_c_function(ctx, js_alert, 1);
    duk_put_global_string(ctx, "alert");
}

/* Evaluate file from argv[1]. Return DUK_EXEC_SUCCESS or error code. */
static duk_int_t eval_file(duk_context* ctx, const char* path){
    FILE* f=fopen(path, "rb");
    if(!f){
        duk_push_error_object(ctx, DUK_ERR_ERROR, "No file: %s", path);
        return DUK_EXEC_ERROR;
    }
    fseek(f, 0, SEEK_END);
    long sz=ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf=(char*)malloc(sz+1);
    if(!buf){
        fclose(f);
        duk_push_error_object(ctx, DUK_ERR_ERROR,"OOM reading: %s", path);
        return DUK_EXEC_ERROR;
    }
    size_t rd=fread(buf,1,sz,f);
    fclose(f);
    if(rd!=(size_t)sz){
        free(buf);
        duk_push_error_object(ctx,DUK_ERR_ERROR,"Bad read: %s", path);
        return DUK_EXEC_ERROR;
    }
    buf[sz]=0;
    duk_int_t rc=duk_peval_string(ctx,buf);
    free(buf);
    return rc;
}

int main(int argc, char** argv){
    if(argc<2){
        fprintf(stderr,"Usage: %s <script.js>\n", argv[0]);
        return 1;
    }
    if(SDL_Init(SDL_INIT_VIDEO)<0){
        fprintf(stderr,"SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }
    IMG_Init(IMG_INIT_PNG|IMG_INIT_JPG);

    g_window = SDL_CreateWindow("Canvas Demo", SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
                                g_win_w, g_win_h, 0);
    if(!g_window){
        fprintf(stderr,"Failed SDL_CreateWindow\n");
        return 1;
    }
    g_renderer = SDL_CreateRenderer(g_window, -1,
                    SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!g_renderer){
        fprintf(stderr,"Failed SDL_CreateRenderer\n");
        return 1;
    }

    /* Clear once at start. If user wants repeated clearing, they'd do so in JS. */
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderPresent(g_renderer);

    duk_context* ctx = duk_create_heap_default();
    duk_console_init(ctx, DUK_CONSOLE_PROXY_WRAPPER);

    create_document(ctx);
    create_image(ctx);
    create_window(ctx);
    create_alert(ctx);

    /* Load user's script, e.g. "canvas_loop.js" or "canvas.js" */
    duk_int_t rc=eval_file(ctx, argv[1]);
    if(rc==DUK_EXEC_SUCCESS){
        duk_pop(ctx);
        /* If window.onload is set, call it once */
        call_window_onload(ctx);
    } else {
        fprintf(stderr,"Script error: %s\n", duk_safe_to_string(ctx,-1));
        duk_pop(ctx);
    }

    int running=1;
    while(running){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT) running=0;
        }
        /* check setInterval timers each loop */
        check_intervals(ctx);
        SDL_Delay(10);
    }

    duk_destroy_heap(ctx);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
