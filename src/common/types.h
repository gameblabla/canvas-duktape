#pragma once
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Shared data types (no external dependencies)
 * ============================================================================ */

#define MAX_INTERVALS     1024
#define MAX_SCRIPTS       32
#define MAX_IMAGES        1024
#define MAX_STORAGE_ITEMS 256
#define MAX_KEY_LISTENERS 16

typedef struct {
    char id[256];
    int  width;
    int  height;
    char style[512];
} CanvasInfo;

typedef struct {
    char id[256];
    char src[512];
    int  width;
    int  height;
} ImageInfo;

typedef struct {
    char  src[512];
    char* inline_code;
    int   is_inline;
} ScriptInfo;

/* ============================================================================
 * Renderer Interface
 * Implemented by: src/renderer/sdl2/renderer_sdl2.c
 * Used by:        src/jscore/duktape/jscore_duk.c  and  src/main.c
 * ============================================================================ */
typedef struct {
    /* Lifecycle */
    int    (*init)(int w, int h, const char* title);
    void   (*quit)(void);

    /* Texture (canvas) management — opaque void* handles */
    void*  (*create_texture)(int w, int h);
    void   (*destroy_texture)(void* tex);
    void*  (*get_main_texture)(void);

    /* Image loading */
    void*  (*load_image_file)(const char* path);
    void*  (*load_image_mem)(const unsigned char* data, int len);
    void   (*destroy_image)(void* img);
    void   (*get_image_size)(void* img, int* w, int* h);

    /* Solid / pattern fills */
    void   (*fill_rect)(void* target, int x, int y, int w, int h,
                        uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                        int blend_add, const double* m);
    void   (*fill_rect_pattern)(void* target, int x, int y, int w, int h,
                                void* img, uint8_t alpha);
    void   (*clear_rect)(void* target, int x, int y, int w, int h);
    void   (*stroke_rect)(void* target, double x, double y, double w, double h,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a, int lw,
                          int blend_add);

    /* Image / canvas blitting */
    void   (*draw_image)(void* target, void* img,
                         int sx, int sy, int sw, int sh,
                         int dx, int dy, int dw, int dh,
                         const double* m, uint8_t alpha);
    void   (*draw_canvas)(void* target, void* src_tex,
                          int sx, int sy, int sw, int sh,
                          int dx, int dy, int dw, int dh,
                          const double* m, uint8_t alpha);

    /* Text */
    void   (*fill_text)(void* target, const char* text, double x, double y,
                        uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                        int font_size, const char* align, const char* baseline,
                        const char* font_family);
    void   (*stroke_text)(void* target, const char* text, double x, double y,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                          int font_size, int lw, const char* font_family);
    int    (*measure_text)(const char* text, int font_size, const char* font_family);
    /* New: returns width, sets *ascent and *descent via out params */
    void   (*measure_text_ex)(const char* text, int font_size, const char* font_family,
                              int* out_width, int* out_ascent, int* out_descent);

    /* Path / shape drawing */
    void   (*draw_arc_points)(void* target,
                              double cx, double cy, double radius,
                              double start_angle, double end_angle, int ccw,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void   (*fill_polygon)(void* target, const double* pts, int count,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                           int blend_add, int fill_rule);
    void   (*fill_circle)(void* target,
                          double cx, double cy, int radius,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                          int blend_add);
    void   (*stroke_circle)(void* target,
                            double cx, double cy, int radius,
                            uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void   (*draw_line)(void* target,
                        int x1, int y1, int x2, int y2,
                        uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    /* Clipping */
    void   (*set_clip_rect)(void* target, int x, int y, int w, int h);
    void   (*clear_clip_rect)(void* target);

    /* Pixel I/O */
    void   (*get_pixels)(void* target, int x, int y, int w, int h,
                         uint8_t* rgba_out);
    void   (*put_pixels)(void* target, const uint8_t* rgba,
                         int x, int y, int w, int h);

    /* Export */
    char*  (*to_data_url)(void* target, int w, int h); /* caller frees */

    /* Frame */
    void   (*present)(void);
    void   (*clear_main)(void);

    /* Time */
    double (*get_time_ms)(void);
    void   (*sleep_ms)(int ms);
    
    /* Screenshot */
    int    (*screenshot)(const char* filename);
} RendererInterface;

/* ============================================================================
 * Input Interface
 * Implemented by: src/input/SDL2/input_sdl2.c
 * ============================================================================ */
typedef enum {
    INPUT_EVENT_NONE      = 0,
    INPUT_EVENT_QUIT      = 1,
    INPUT_EVENT_KEYDOWN   = 2,
    INPUT_EVENT_KEYUP     = 3,
    INPUT_EVENT_MOUSEMOVE = 4,
    INPUT_EVENT_MOUSEDOWN = 5,
    INPUT_EVENT_MOUSEUP   = 6
} InputEventType;

typedef struct {
    InputEventType type;
    int keycode;   /* browser-style keyCode */
    int x, y;     /* mouse position in window pixels */
    int button;   /* mouse button: 0=left, 1=middle, 2=right */
} InputEvent;

typedef struct {
    void (*init)(void);
    void (*quit)(void);
    int  (*poll)(InputEvent* out);  /* 1 = got event, 0 = queue empty */
} InputInterface;

/* ============================================================================
 * Sound Interface
 * Implemented by: src/sound/SDL2/sound_sdl2.c
 * ============================================================================ */
typedef struct {
    void (*init)(void);
    void (*quit)(void);
} SoundInterface;

/* ============================================================================
 * JS Core Interface
 * Implemented by: src/jscore/duktape/jscore_duk.c
 * ============================================================================ */
typedef struct {
    int  (*init)(RendererInterface* renderer,
                 InputInterface*   input,
                 SoundInterface*   sound);
    void (*quit)(void);

    void (*setup_globals)(int win_w, int win_h,
                          CanvasInfo* canvases, int canvas_count,
                          ImageInfo*  images,  int image_count,
                          const char* window_title);
    void (*preload_images)(ImageInfo* images, int count);

    int  (*eval_file)(const char* path);
    int  (*eval_string)(const char* code);

    void (*call_window_onload)(void);
    void (*call_window_load_listeners)(void);

    void (*check_timers)(void);
    void (*dispatch_key)(int keycode, int is_down);
    void (*dispatch_mouse)(int event_type, int x, int y, int button);
    
    /* Optional: enable broken WebGL support */
    void (*set_broken_webgl)(int enable);
} JSCoreInterface;
