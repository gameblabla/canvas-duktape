#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "duktape.h"
#include "duktape/extras/console/duk_console.h"
#include "jscore_duk.h"

/* ============================================================================
 * Module-level state
 * ============================================================================ */
static RendererInterface* g_R   = NULL; /* renderer */
static InputInterface*    g_IN  = NULL; /* input    */
static SoundInterface*    g_SND = NULL; /* sound    */
static duk_context*       g_ctx = NULL;

static int g_win_w = 120, g_win_h = 160;

static CanvasInfo g_canvas_info[MAX_IMAGES];
static int        g_canvas_count = 0;
static ImageInfo  g_image_info[MAX_IMAGES];
static int        g_image_count  = 0;

/* ============================================================================
 * Internal types (opaque to other modules)
 * ============================================================================ */
typedef struct {
    void* tex;          /* opaque renderer texture handle */
    int   w, h;
    char  fname[512];
} MyImage;

typedef struct {
    int   width, height;
    void* texture;      /* opaque renderer texture handle */
} MyCanvas;

typedef struct {
    int    active;
    double intervalMs;  /* negative = one-shot timeout */
    double nextTime;
    int    funcId;
} IntervalInfo;

typedef struct {
    char key[256];
    char value[1024];
} StorageItem;

/* ============================================================================
 * Canvas 2D context state
 * ============================================================================ */
static struct {
    double path_x, path_y, path_w, path_h;
    int    has_rect_path; /* 0=none,1=rect,2=arc,3=line,4=complex */
    double m[6];          /* transform: a,b,c,d,e,f */
    int    has_clip;
    int    clip_type;
    int    clip_x, clip_y, clip_w, clip_h;
    double path_points[512];
    int    path_point_count;
    double path_start_x, path_start_y;
    double user_x, user_y;
} g_cs;

#define MAX_STATE_STACK 32
static struct {
    char   fillStyle[64];
    char   strokeStyle[64];
    double lineWidth;
    double globalAlpha;
    double transform[6];
    int    has_clip;
    int    clip_type;
    int    clip_x, clip_y, clip_w, clip_h;
} g_ss[MAX_STATE_STACK];
static int g_ss_top = 0;

/* ============================================================================
 * Timers / intervals
 * ============================================================================ */
static IntervalInfo g_intervals[MAX_INTERVALS];
static int g_func_id_gen = 0;
static int g_window_onload_funcId = -1;
static int g_window_load_funcId   = -1;

/* ============================================================================
 * Key listeners
 * ============================================================================ */
static int g_keydown_listeners[MAX_KEY_LISTENERS];
static int g_keydown_listener_count = 0;
static int g_keyup_listeners[MAX_KEY_LISTENERS];
static int g_keyup_listener_count   = 0;

/* ============================================================================
 * localStorage
 * ============================================================================ */
static StorageItem g_storage[MAX_STORAGE_ITEMS];
static int         g_storage_count = 0;

/* ============================================================================
 * Forward declarations
 * ============================================================================ */
static duk_ret_t js_doc_addEventListener(duk_context* ctx);
static duk_ret_t js_win_addEventListener(duk_context* ctx);
static duk_ret_t js_fillRect(duk_context* ctx);
static duk_ret_t js_clearRect(duk_context* ctx);
static duk_ret_t js_getImageData(duk_context* ctx);
static duk_ret_t js_getImageDataHD(duk_context* ctx);
static duk_ret_t js_save(duk_context* ctx);
static duk_ret_t js_restore(duk_context* ctx);
static duk_ret_t js_scale(duk_context* ctx);
static duk_ret_t js_translate(duk_context* ctx);
static duk_ret_t js_rotate(duk_context* ctx);
static duk_ret_t js_fillText(duk_context* ctx);
static duk_ret_t js_strokeText(duk_context* ctx);
static duk_ret_t js_measureText(duk_context* ctx);
static duk_ret_t js_strokeRect(duk_context* ctx);
static duk_ret_t js_beginPath(duk_context* ctx);
static duk_ret_t js_moveTo(duk_context* ctx);
static duk_ret_t js_lineTo(duk_context* ctx);
static duk_ret_t js_closePath(duk_context* ctx);
static duk_ret_t js_fill(duk_context* ctx);
static duk_ret_t js_stroke(duk_context* ctx);
static duk_ret_t js_clip(duk_context* ctx);
static duk_ret_t js_bezierCurveTo(duk_context* ctx);
static duk_ret_t js_rect(duk_context* ctx);
static duk_ret_t js_arc(duk_context* ctx);
static duk_ret_t js_arcTo(duk_context* ctx);
static duk_ret_t js_quadraticCurveTo(duk_context* ctx);
static duk_ret_t js_createLinearGradient(duk_context* ctx);
static duk_ret_t js_gradient_addColorStop(duk_context* ctx);
static duk_ret_t js_createRadialGradient(duk_context* ctx);
static duk_ret_t js_createPattern(duk_context* ctx);
static duk_ret_t js_setTransform(duk_context* ctx);
static duk_ret_t js_transform(duk_context* ctx);
static duk_ret_t js_putImageData(duk_context* ctx);
static duk_ret_t js_createImageData(duk_context* ctx);
static duk_ret_t js_isPointInPath(duk_context* ctx);
static duk_ret_t js_toDataURL(duk_context* ctx);
static duk_ret_t js_getElementById(duk_context* ctx);
static duk_ret_t js_getElementsByTagName(duk_context* ctx);
static duk_ret_t js_createElement(duk_context* ctx);
static duk_ret_t js_html5_audio(duk_context* ctx);
static duk_ret_t js_audio_load(duk_context* ctx);
static duk_ret_t js_audio_play(duk_context* ctx);
static duk_ret_t js_audio_pause(duk_context* ctx);
static duk_ret_t js_audio_canPlayType(duk_context* ctx);
static duk_ret_t js_audio_addEventListener(duk_context* ctx);
static duk_ret_t js_requestAnimationFrame(duk_context* ctx);
static duk_ret_t js_cancelAnimationFrame(duk_context* ctx);
static duk_ret_t js_html_element_ctor(duk_context* ctx);
static duk_ret_t js_img_width_getter(duk_context* ctx);
static duk_ret_t js_img_height_getter(duk_context* ctx);
static duk_ret_t js_drawImage(duk_context* ctx);
static duk_ret_t js_noop(duk_context* ctx);
static void* get_current_canvas_texture(duk_context* ctx);

/* ============================================================================
 * Utility: function storage (heap stash)
 * ============================================================================ */
static int store_func(duk_context* ctx, int funcIndex) {
    duk_push_heap_stash(ctx);
    duk_get_prop_string(ctx, -1, "g_funcStore");
    if (duk_is_undefined(ctx, -1)) {
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

static void push_stored_func(duk_context* ctx, int fid) {
    duk_push_heap_stash(ctx);
    duk_get_prop_string(ctx, -1, "g_funcStore");
    if (duk_is_undefined(ctx, -1)) {
        duk_pop_2(ctx);
        duk_push_undefined(ctx);
        return;
    }
    duk_push_int(ctx, fid);
    duk_get_prop(ctx, -2);
    duk_remove(ctx, -2);
    duk_remove(ctx, -2);
}

/* ============================================================================
 * Utility: CSS color parsing
 * ============================================================================ */
static void parse_css_hex(const char* color, uint8_t* r, uint8_t* g, uint8_t* b) {
    int len = (int)strlen(color + 1);
    if (len == 3) {
        int ri, gi, bi;
        sscanf(color + 1, "%1x%1x%1x", &ri, &gi, &bi);
        *r = (uint8_t)(ri * 17);
        *g = (uint8_t)(gi * 17);
        *b = (uint8_t)(bi * 17);
    } else {
        sscanf(color + 1, "%2hhx%2hhx%2hhx", r, g, b);
    }
}

static void parse_color(const char* color,
                         uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    *r = 0; *g = 0; *b = 0; *a = 255;
    if (!color) return;
    if (color[0] == '#') {
        parse_css_hex(color, r, g, b);
    } else if (strncmp(color, "rgba(", 5) == 0) {
        int ri, gi, bi; float af;
        sscanf(color + 5, "%d,%d,%d,%f", &ri, &gi, &bi, &af);
        *r=(uint8_t)ri; *g=(uint8_t)gi; *b=(uint8_t)bi;
        *a=(uint8_t)(af * 255.0f);
    } else if (strncmp(color, "rgb(", 4) == 0) {
        int ri, gi, bi;
        sscanf(color + 4, "%d,%d,%d", &ri, &gi, &bi);
        *r=(uint8_t)ri; *g=(uint8_t)gi; *b=(uint8_t)bi;
    }
}

/* ============================================================================
 * Utility: base64 decode (for data: image URLs)
 * ============================================================================ */
static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
static unsigned char* base64_decode(const char* src, size_t* out_len) {
    size_t src_len = strlen(src);
    while (src_len > 0 && src[src_len-1] == '=') src_len--;
    size_t out_size = (src_len * 3) / 4 + 4;
    unsigned char* out = malloc(out_size);
    size_t pos = 0;
    for (size_t i = 0; i < src_len; i += 4) {
        int v1 = b64_val(src[i]);
        int v2 = (i+1 < src_len) ? b64_val(src[i+1]) : 0;
        int v3 = (i+2 < src_len) ? b64_val(src[i+2]) : -1;
        int v4 = (i+3 < src_len) ? b64_val(src[i+3]) : -1;
        if (v1 < 0 || v2 < 0) break;
        out[pos++] = (v1 << 2) | (v2 >> 4);
        if (v3 >= 0) {
            out[pos++] = ((v2 & 0x0f) << 4) | (v3 >> 2);
            if (v4 >= 0) out[pos++] = ((v3 & 0x03) << 6) | v4;
        }
    }
    *out_len = pos;
    return out;
}

/* ============================================================================
 * Canvas context helpers
 * ============================================================================ */
static void* get_current_canvas_texture(duk_context* ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "\xFF""canvasPtr");
    MyCanvas* canvas = (MyCanvas*)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);
    if (canvas && canvas->texture) return canvas->texture;
    return g_R->get_main_texture();
}

static double get_global_alpha(duk_context* ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "globalAlpha");
    double a = duk_is_number(ctx, -1) ? duk_get_number(ctx, -1) : 1.0;
    duk_pop_2(ctx);
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;
    return a;
}

static int get_global_composite(duk_context* ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "globalCompositeOperation");
    const char* op = duk_get_string(ctx, -1);
    duk_pop_2(ctx);
    return (op && strcmp(op, "lighter") == 0) ? 1 : 0;
}

/* Parse font size from "48px Arial" → 48, default 20. */
static int parse_font_size(const char* font_str) {
    if (!font_str) return 20;
    const char* px = strstr(font_str, "px");
    if (!px) return 20;
    char buf[16]; int i = 0;
    const char* s = px;
    while (s > font_str && i < 15 && *(s-1) >= '0' && *(s-1) <= '9') s--;
    while (*s >= '0' && *s <= '9' && s < px && i < 15) buf[i++] = *s++;
    buf[i] = '\0';
    return (i > 0) ? atoi(buf) : 20;
}

/* ============================================================================
 * Image handling
 * ============================================================================ */
static MyImage* get_image_ptr(duk_context* ctx, int idx) {
    duk_get_prop_string(ctx, idx, "\xFF""ptr");
    MyImage* p = (MyImage*)duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    return p;
}

static void load_sync(MyImage* img) {
    if (img->tex || !img->fname[0]) return;
    img->tex = g_R->load_image_file(img->fname);
    if (img->tex) g_R->get_image_size(img->tex, &img->w, &img->h);
    else fprintf(stderr, "[load_sync] failed: %s\n", img->fname);
}

static void call_img_listeners(duk_context* ctx, int obj_idx, const char* ev) {
    duk_get_prop_string(ctx, obj_idx, "listeners");
    if (!duk_is_object(ctx, -1)) { duk_pop(ctx); return; }
    duk_get_prop_string(ctx, -1, ev);
    if (!duk_is_array(ctx, -1)) { duk_pop_2(ctx); return; }
    duk_uarridx_t n = (duk_uarridx_t)duk_get_length(ctx, -1);
    for (duk_uarridx_t i = 0; i < n; i++) {
        duk_get_prop_index(ctx, -1, i);
        if (duk_is_callable(ctx, -1)) {
            duk_dup(ctx, obj_idx);
            if (duk_pcall_method(ctx, 0) != 0)
                fprintf(stderr, "[img_listeners] %s error: %s\n",
                        ev, duk_safe_to_string(ctx, -1));
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
    if (!duk_is_object(ctx, -1)) {
        duk_pop(ctx);
        duk_push_object(ctx);
        duk_put_prop_string(ctx, -2, "listeners");
        duk_get_prop_string(ctx, -1, "listeners");
    }
    duk_get_prop_string(ctx, -1, evName);
    if (!duk_is_array(ctx, -1)) {
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

static duk_ret_t js_img_src_setter(duk_context* ctx) {
    duk_push_this(ctx);
    MyImage* img = get_image_ptr(ctx, -1);
    if (!img) { duk_pop(ctx); return 0; }
    const char* fn = duk_require_string(ctx, 0);
    memset(img->fname, 0, sizeof(img->fname));
    strncpy(img->fname, fn, sizeof(img->fname)-1);
    if (img->tex) { g_R->destroy_image(img->tex); img->tex = NULL; }

    int load_ok = 0;
    if (strncmp(fn, "data:image/png;base64,", 22) == 0) {
        const char* b64 = fn + 22;
        size_t png_len;
        unsigned char* png_data = base64_decode(b64, &png_len);
        if (png_data && png_len > 0) {
            img->tex = g_R->load_image_mem(png_data, (int)png_len);
            if (img->tex) {
                g_R->get_image_size(img->tex, &img->w, &img->h);
                load_ok = 1;
            }
            free(png_data);
        }
    } else {
        load_sync(img);
        if (img->tex) load_ok = 1;
    }

    if (img->tex) {
        call_img_listeners(ctx, -1, "load");
        duk_get_prop_string(ctx, -1, "onload");
        if (duk_is_callable(ctx, -1)) {
            duk_dup(ctx, -2);
            if (duk_pcall(ctx, 1) != 0)
                fprintf(stderr, "[Image.onload] %s\n", duk_safe_to_string(ctx,-1));
            duk_pop(ctx);
        } else duk_pop(ctx);
    } else if (!load_ok) {
        fprintf(stderr, "[Image.src] failed to load\n");
        duk_get_prop_string(ctx, -1, "onerror");
        if (duk_is_callable(ctx, -1)) {
            duk_dup(ctx, -2);
            duk_push_object(ctx);
            duk_push_string(ctx, "error");
            duk_put_prop_string(ctx, -2, "type");
            if (duk_pcall(ctx, 1) != 0)
                fprintf(stderr, "[Image.onerror] %s\n", duk_safe_to_string(ctx,-1));
            duk_pop(ctx);
        } else duk_pop(ctx);
    }
    duk_pop(ctx);
    return 0;
}

static duk_ret_t js_img_src_getter(duk_context* ctx) {
    duk_push_this(ctx);
    MyImage* img = get_image_ptr(ctx, -1);
    duk_pop(ctx);
    duk_push_string(ctx, (img && img->fname[0]) ? img->fname : "");
    return 1;
}

static duk_ret_t js_img_width_getter(duk_context* ctx) {
    duk_push_this(ctx);
    MyImage* img = get_image_ptr(ctx, -1);
    duk_pop(ctx);
    duk_push_int(ctx, img ? img->w : 0);
    return 1;
}

static duk_ret_t js_img_height_getter(duk_context* ctx) {
    duk_push_this(ctx);
    MyImage* img = get_image_ptr(ctx, -1);
    duk_pop(ctx);
    duk_push_int(ctx, img ? img->h : 0);
    return 1;
}

static duk_ret_t ImageCtor(duk_context* ctx) {
    duk_push_object(ctx);
    MyImage* im = (MyImage*)calloc(1, sizeof(MyImage));
    duk_push_pointer(ctx, im);
    duk_put_prop_string(ctx, -2, "\xFF""ptr");
    duk_push_c_function(ctx, js_img_addEventListener, 2);
    duk_put_prop_string(ctx, -2, "addEventListener");
    duk_push_string(ctx, "src");
    duk_push_c_function(ctx, js_img_src_getter, 0);
    duk_push_c_function(ctx, js_img_src_setter, 1);
    duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER |
                          DUK_DEFPROP_ENUMERABLE);
    duk_push_string(ctx, "width");
    duk_push_c_function(ctx, js_img_width_getter, 0);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_ENUMERABLE);
    duk_push_string(ctx, "height");
    duk_push_c_function(ctx, js_img_height_getter, 0);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_ENUMERABLE);
    return 1;
}

static void create_image(duk_context* ctx) {
    duk_push_c_function(ctx, ImageCtor, 0);
    duk_put_global_string(ctx, "Image");
}

/* ============================================================================
 * Canvas property getters/setters
 * ============================================================================ */
static MyCanvas* get_canvas_ptr(duk_context* ctx, int idx) {
    duk_get_prop_string(ctx, idx, "\xFF""canvasPtr");
    MyCanvas* p = (MyCanvas*)duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    return p;
}

static void reset_canvas_state(duk_context* ctx) {
    g_cs.m[0]=1; g_cs.m[1]=0; g_cs.m[2]=0;
    g_cs.m[3]=1; g_cs.m[4]=0; g_cs.m[5]=0;
    g_cs.has_clip = 0;
    g_R->clear_clip_rect(g_R->get_main_texture());
    duk_push_this(ctx);
    duk_push_number(ctx, 1.0);
    duk_put_prop_string(ctx, -2, "globalAlpha");
    duk_pop(ctx);
    g_ss_top = 0;
}

static duk_ret_t js_canvas_width_getter(duk_context* ctx) {
    duk_push_this(ctx);
    MyCanvas* c = get_canvas_ptr(ctx, -1);
    duk_pop(ctx);
    duk_push_int(ctx, c ? c->width : 0);
    return 1;
}

static duk_ret_t js_canvas_width_setter(duk_context* ctx) {
    duk_push_this(ctx);
    MyCanvas* c = get_canvas_ptr(ctx, -1);
    duk_pop(ctx);
    if (!c) return 0;
    c->width = duk_require_int(ctx, 0);
    if (c->texture) g_R->destroy_texture(c->texture);
    c->texture = g_R->create_texture(c->width, c->height);
    reset_canvas_state(ctx);
    return 0;
}

static duk_ret_t js_canvas_height_getter(duk_context* ctx) {
    duk_push_this(ctx);
    MyCanvas* c = get_canvas_ptr(ctx, -1);
    duk_pop(ctx);
    duk_push_int(ctx, c ? c->height : 0);
    return 1;
}

static duk_ret_t js_canvas_height_setter(duk_context* ctx) {
    duk_push_this(ctx);
    MyCanvas* c = get_canvas_ptr(ctx, -1);
    duk_pop(ctx);
    if (!c) return 0;
    c->height = duk_require_int(ctx, 0);
    if (c->texture) g_R->destroy_texture(c->texture);
    c->texture = g_R->create_texture(c->width, c->height);
    reset_canvas_state(ctx);
    return 0;
}

/* ============================================================================
 * Drawing: drawImage
 * ============================================================================ */
static duk_ret_t js_drawImage(duk_context* ctx) {
    int nargs = duk_get_top(ctx);
    if (nargs != 3 && nargs != 5 && nargs != 9)
        return duk_error(ctx, DUK_ERR_TYPE_ERROR,
                         "drawImage requires 3, 5, or 9 args, got %d", nargs);

    double globalAlpha = get_global_alpha(ctx);
    uint8_t alphaMod = (uint8_t)(globalAlpha * 255.0 + 0.5);

    /* Check if source is a canvas */
    duk_dup(ctx, 0);
    duk_get_prop_string(ctx, -1, "\xFF""canvasPtr");
    MyCanvas* src_canvas = (MyCanvas*)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);

    void* target = get_current_canvas_texture(ctx);

    if (src_canvas) {
        int sw = src_canvas->width, sh = src_canvas->height;
        int sx=0,sy=0, dx=0,dy=0, dw=sw, dh=sh;
        if      (nargs==3){ dx=(int)duk_require_number(ctx,1); dy=(int)duk_require_number(ctx,2); }
        else if (nargs==5){ dx=(int)duk_require_number(ctx,1); dy=(int)duk_require_number(ctx,2);
                            dw=(int)duk_require_number(ctx,3); dh=(int)duk_require_number(ctx,4); }
        else if (nargs==9){ sx=(int)duk_require_number(ctx,1); sy=(int)duk_require_number(ctx,2);
                            sw=(int)duk_require_number(ctx,3); sh=(int)duk_require_number(ctx,4);
                            dx=(int)duk_require_number(ctx,5); dy=(int)duk_require_number(ctx,6);
                            dw=(int)duk_require_number(ctx,7); dh=(int)duk_require_number(ctx,8); }
        if (target && src_canvas->texture)
            g_R->draw_canvas(target, src_canvas->texture,
                             sx,sy,sw,sh, dx,dy,dw,dh, g_cs.m, alphaMod);
        return 0;
    }

    MyImage* im = get_image_ptr(ctx, 0);
    if (!im) return 0;
    load_sync(im);
    if (!im->tex) return 0;

    int sx=0,sy=0, sw=im->w, sh=im->h;
    int dx=0,dy=0, dw=im->w, dh=im->h;
    if      (nargs==3){ dx=(int)duk_require_number(ctx,1); dy=(int)duk_require_number(ctx,2); }
    else if (nargs==5){ dx=(int)duk_require_number(ctx,1); dy=(int)duk_require_number(ctx,2);
                        dw=(int)duk_require_number(ctx,3); dh=(int)duk_require_number(ctx,4); }
    else if (nargs==9){ sx=(int)duk_require_number(ctx,1); sy=(int)duk_require_number(ctx,2);
                        sw=(int)duk_require_number(ctx,3); sh=(int)duk_require_number(ctx,4);
                        dx=(int)duk_require_number(ctx,5); dy=(int)duk_require_number(ctx,6);
                        dw=(int)duk_require_number(ctx,7); dh=(int)duk_require_number(ctx,8); }

    if (sw<=0||sh<=0||dw<=0||dh<=0) return 0;
    if (sx<0){ dx-=(int)(sx*(double)dw/sw); dw+=(int)(sx*(double)dw/sw); sw+=sx; sx=0; }
    if (sy<0){ dy-=(int)(sy*(double)dh/sh); dh+=(int)(sy*(double)dh/sh); sh+=sy; sy=0; }
    if (sx+sw>im->w) sw=im->w-sx;
    if (sy+sh>im->h) sh=im->h-sy;
    if (sw<=0||sh<=0) return 0;

    if (target)
        g_R->draw_image(target, im->tex,
                        sx,sy,sw,sh, dx,dy,dw,dh, g_cs.m, alphaMod);
    return 0;
}

/* ============================================================================
 * Drawing: fillRect, clearRect
 * ============================================================================ */
static duk_ret_t js_fillRect(duk_context* ctx) {
    if (duk_get_top(ctx) < 4)
        return duk_error(ctx, DUK_ERR_TYPE_ERROR, "fillRect: 4 args needed");
    int x=duk_require_int(ctx,0), y=duk_require_int(ctx,1);
    int w=duk_require_int(ctx,2), h=duk_require_int(ctx,3);

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "fillStyle");
    int is_pattern = duk_is_object(ctx, -1);
    void* pattern_img = NULL;
    if (is_pattern) {
        duk_get_prop_string(ctx, -1, "\xFF""image");
        MyImage* pimg = (MyImage*)duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        if (pimg) {
            load_sync(pimg);
            pattern_img = pimg->tex;
        }
    }
    const char* color = is_pattern ? NULL : duk_get_string(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, "globalAlpha");
    double ga = duk_get_number(ctx, -1);
    duk_pop_2(ctx);

    int use_lighter = get_global_composite(ctx);
    void* target = get_current_canvas_texture(ctx);

    if (is_pattern && pattern_img) {
        g_R->fill_rect_pattern(target, x, y, w, h, pattern_img);
    } else if (!is_pattern && color) {
        uint8_t r=0,g=0,b=0,a=255;
        parse_color(color, &r, &g, &b, &a);
        a = (uint8_t)(a * ga);
        g_R->fill_rect(target, x, y, w, h, r, g, b, a, use_lighter, g_cs.m);
    }
    return 0;
}

static duk_ret_t js_clearRect(duk_context* ctx) {
    if (duk_get_top(ctx) < 4)
        return duk_error(ctx, DUK_ERR_TYPE_ERROR, "clearRect: 4 args");
    int x=duk_require_int(ctx,0), y=duk_require_int(ctx,1);
    int w=duk_require_int(ctx,2), h=duk_require_int(ctx,3);
    void* target = get_current_canvas_texture(ctx);
    g_R->clear_rect(target, x, y, w, h);
    return 0;
}

/* ============================================================================
 * Drawing: getImageData, putImageData, createImageData
 * ============================================================================ */
static duk_ret_t js_getImageData(duk_context* ctx) {
    int x=duk_require_int(ctx,0), y=duk_require_int(ctx,1);
    int w=duk_require_int(ctx,2), h=duk_require_int(ctx,3);
    duk_push_object(ctx);
    duk_push_int(ctx, w); duk_put_prop_string(ctx,-2,"width");
    duk_push_int(ctx, h); duk_put_prop_string(ctx,-2,"height");
    duk_push_array(ctx);
    void* target = get_current_canvas_texture(ctx);
    if (target) {
        uint8_t* pixels = malloc((size_t)w * h * 4);
        if (pixels) {
            g_R->get_pixels(target, x, y, w, h, pixels);
            for (int i = 0; i < w*h; i++) {
                duk_push_int(ctx, pixels[i*4+0]); duk_put_prop_index(ctx,-2,i*4);
                duk_push_int(ctx, pixels[i*4+1]); duk_put_prop_index(ctx,-2,i*4+1);
                duk_push_int(ctx, pixels[i*4+2]); duk_put_prop_index(ctx,-2,i*4+2);
                duk_push_int(ctx, pixels[i*4+3]); duk_put_prop_index(ctx,-2,i*4+3);
            }
            free(pixels);
        }
    }
    duk_put_prop_string(ctx, -2, "data");
    return 1;
}

static duk_ret_t js_getImageDataHD(duk_context* ctx) {
    return js_getImageData(ctx);
}

static duk_ret_t js_putImageData(duk_context* ctx) {
    int nargs = duk_get_top(ctx);
    if (nargs < 3) return duk_error(ctx, DUK_ERR_TYPE_ERROR, "putImageData: 3 args");
    duk_dup(ctx, 0);
    duk_get_prop_string(ctx,-1,"width"); int w=duk_get_int(ctx,-1); duk_pop(ctx);
    duk_get_prop_string(ctx,-1,"height"); int h=duk_get_int(ctx,-1); duk_pop(ctx);
    duk_pop(ctx);
    int dx=(int)duk_get_number(ctx,1), dy=(int)duk_get_number(ctx,2);
    int dirtyX=0,dirtyY=0,dirtyW=w,dirtyH=h;
    int useDirty=0;
    if (nargs >= 7) {
        dirtyX=duk_get_int(ctx,3); dirtyY=duk_get_int(ctx,4);
        dirtyW=duk_get_int(ctx,5); dirtyH=duk_get_int(ctx,6);
        useDirty=1;
    }
    void* target = get_current_canvas_texture(ctx);
    if (target) {
        duk_get_prop_string(ctx, 0, "data");
        duk_uarridx_t dataLen = (duk_uarridx_t)duk_get_length(ctx,-1);
        int srcX = useDirty?dirtyX:0, srcY = useDirty?dirtyY:0;
        int destX= dx + (useDirty?dirtyX:0), destY= dy + (useDirty?dirtyY:0);
        int copyW= useDirty?dirtyW:w, copyH= useDirty?dirtyH:h;
        uint8_t* buf = malloc((size_t)copyW * copyH * 4);
        if (buf) {
            for (int py=0;py<copyH;py++) for (int px=0;px<copyW;px++) {
                int sx=srcX+px, sy=srcY+py;
                uint8_t r=0,g=0,b=0,a=0;
                if (sx>=0&&sx<w&&sy>=0&&sy<h) {
                    int idx=(sy*w+sx)*4;
                    if (idx+3 < (int)dataLen) {
                        duk_get_prop_index(ctx,-1,idx);   r=(uint8_t)duk_get_int(ctx,-1);duk_pop(ctx);
                        duk_get_prop_index(ctx,-1,idx+1); g=(uint8_t)duk_get_int(ctx,-1);duk_pop(ctx);
                        duk_get_prop_index(ctx,-1,idx+2); b=(uint8_t)duk_get_int(ctx,-1);duk_pop(ctx);
                        duk_get_prop_index(ctx,-1,idx+3); a=(uint8_t)duk_get_int(ctx,-1);duk_pop(ctx);
                    }
                }
                buf[(py*copyW+px)*4+0]=r;
                buf[(py*copyW+px)*4+1]=g;
                buf[(py*copyW+px)*4+2]=b;
                buf[(py*copyW+px)*4+3]=a;
            }
            g_R->put_pixels(target, buf, destX, destY, copyW, copyH);
            free(buf);
        }
        duk_pop(ctx);
    }
    return 0;
}

static duk_ret_t js_createImageData(duk_context* ctx) {
    int nargs=duk_get_top(ctx), w, h;
    if (nargs>=2) { w=duk_get_int(ctx,0); h=duk_get_int(ctx,1); }
    else if (nargs==1) {
        duk_dup(ctx,0);
        duk_get_prop_string(ctx,-1,"width");  w=duk_get_int(ctx,-1); duk_pop(ctx);
        duk_get_prop_string(ctx,-1,"height"); h=duk_get_int(ctx,-1); duk_pop(ctx);
        duk_pop(ctx);
    } else return duk_error(ctx, DUK_ERR_TYPE_ERROR, "createImageData: args needed");
    duk_push_object(ctx);
    duk_push_int(ctx,w); duk_put_prop_string(ctx,-2,"width");
    duk_push_int(ctx,h); duk_put_prop_string(ctx,-2,"height");
    duk_push_array(ctx);
    for (int i=0;i<w*h*4;i++) { duk_push_int(ctx,0); duk_put_prop_index(ctx,-2,i); }
    duk_put_prop_string(ctx,-2,"data");
    return 1;
}

/* ============================================================================
 * Drawing: save / restore
 * ============================================================================ */
static duk_ret_t js_save(duk_context* ctx) {
    if (g_ss_top >= MAX_STATE_STACK-1) return 0;
    duk_push_this(ctx);
    duk_get_prop_string(ctx,-1,"fillStyle");
    const char* fs=duk_get_string(ctx,-1);
    if(fs) strncpy(g_ss[g_ss_top].fillStyle,fs,63);
    duk_pop(ctx);
    duk_get_prop_string(ctx,-1,"strokeStyle");
    const char* ss=duk_get_string(ctx,-1);
    if(ss) strncpy(g_ss[g_ss_top].strokeStyle,ss,63);
    duk_pop(ctx);
    duk_get_prop_string(ctx,-1,"lineWidth");
    g_ss[g_ss_top].lineWidth=duk_get_number(ctx,-1); duk_pop(ctx);
    duk_get_prop_string(ctx,-1,"globalAlpha");
    g_ss[g_ss_top].globalAlpha=duk_get_number(ctx,-1); duk_pop(ctx);
    duk_pop(ctx);
    for(int i=0;i<6;i++) g_ss[g_ss_top].transform[i]=g_cs.m[i];
    g_ss[g_ss_top].has_clip=g_cs.has_clip;
    g_ss[g_ss_top].clip_type=g_cs.clip_type;
    g_ss[g_ss_top].clip_x=g_cs.clip_x; g_ss[g_ss_top].clip_y=g_cs.clip_y;
    g_ss[g_ss_top].clip_w=g_cs.clip_w; g_ss[g_ss_top].clip_h=g_cs.clip_h;
    g_ss_top++;
    return 0;
}

static duk_ret_t js_restore(duk_context* ctx) {
    if (g_ss_top <= 0) return 0;
    g_ss_top--;
    duk_push_this(ctx);
#define RESTORE_STR(name) \
    duk_push_string(ctx,#name); duk_push_string(ctx,g_ss[g_ss_top].name); \
    duk_def_prop(ctx,-3,DUK_DEFPROP_HAVE_VALUE|DUK_DEFPROP_WRITABLE|DUK_DEFPROP_CONFIGURABLE);
    RESTORE_STR(fillStyle)
    RESTORE_STR(strokeStyle)
#undef RESTORE_STR
    duk_push_string(ctx,"lineWidth");
    duk_push_number(ctx,g_ss[g_ss_top].lineWidth);
    duk_def_prop(ctx,-3,DUK_DEFPROP_HAVE_VALUE|DUK_DEFPROP_WRITABLE|DUK_DEFPROP_CONFIGURABLE);
    duk_push_string(ctx,"globalAlpha");
    duk_push_number(ctx,g_ss[g_ss_top].globalAlpha);
    duk_def_prop(ctx,-3,DUK_DEFPROP_HAVE_VALUE|DUK_DEFPROP_WRITABLE|DUK_DEFPROP_CONFIGURABLE);
    duk_pop(ctx);
    for(int i=0;i<6;i++) g_cs.m[i]=g_ss[g_ss_top].transform[i];
    g_cs.has_clip=g_ss[g_ss_top].has_clip;
    g_cs.clip_type=g_ss[g_ss_top].clip_type;
    g_cs.clip_x=g_ss[g_ss_top].clip_x; g_cs.clip_y=g_ss[g_ss_top].clip_y;
    g_cs.clip_w=g_ss[g_ss_top].clip_w; g_cs.clip_h=g_ss[g_ss_top].clip_h;
    if (g_cs.has_clip)
        g_R->set_clip_rect(g_R->get_main_texture(),
                           g_cs.clip_x, g_cs.clip_y, g_cs.clip_w, g_cs.clip_h);
    return 0;
}

/* ============================================================================
 * Drawing: transform ops
 * ============================================================================ */
static duk_ret_t js_scale(duk_context* ctx) {
    double sx=duk_get_number(ctx,0), sy=duk_get_number(ctx,1);
    g_cs.m[0]*=sx; g_cs.m[1]*=sx; g_cs.m[2]*=sy; g_cs.m[3]*=sy;
    return 0;
}
static duk_ret_t js_translate(duk_context* ctx) {
    double x=duk_get_number(ctx,0), y=duk_get_number(ctx,1);
    g_cs.m[4]+=g_cs.m[0]*x+g_cs.m[2]*y;
    g_cs.m[5]+=g_cs.m[1]*x+g_cs.m[3]*y;
    return 0;
}
static duk_ret_t js_rotate(duk_context* ctx) {
    double a=duk_get_number(ctx,0);
    double ca=cos(a), sa=sin(a);
    double m0=g_cs.m[0]*ca+g_cs.m[2]*sa, m1=g_cs.m[1]*ca+g_cs.m[3]*sa;
    double m2=-g_cs.m[0]*sa+g_cs.m[2]*ca, m3=-g_cs.m[1]*sa+g_cs.m[3]*ca;
    g_cs.m[0]=m0; g_cs.m[1]=m1; g_cs.m[2]=m2; g_cs.m[3]=m3;
    return 0;
}
static duk_ret_t js_setTransform(duk_context* ctx) {
    g_cs.m[0]=duk_get_number(ctx,0); g_cs.m[1]=duk_get_number(ctx,1);
    g_cs.m[2]=duk_get_number(ctx,2); g_cs.m[3]=duk_get_number(ctx,3);
    g_cs.m[4]=duk_get_number(ctx,4); g_cs.m[5]=duk_get_number(ctx,5);
    return 0;
}
static duk_ret_t js_transform(duk_context* ctx) {
    double a=duk_get_number(ctx,0),b=duk_get_number(ctx,1);
    double c=duk_get_number(ctx,2),d=duk_get_number(ctx,3);
    double e=duk_get_number(ctx,4),f=duk_get_number(ctx,5);
    double m0=g_cs.m[0]*a+g_cs.m[2]*b, m1=g_cs.m[1]*a+g_cs.m[3]*b;
    double m2=g_cs.m[0]*c+g_cs.m[2]*d, m3=g_cs.m[1]*c+g_cs.m[3]*d;
    double m4=g_cs.m[0]*e+g_cs.m[2]*f+g_cs.m[4];
    double m5=g_cs.m[1]*e+g_cs.m[3]*f+g_cs.m[5];
    g_cs.m[0]=m0;g_cs.m[1]=m1;g_cs.m[2]=m2;
    g_cs.m[3]=m3;g_cs.m[4]=m4;g_cs.m[5]=m5;
    return 0;
}

/* ============================================================================
 * Drawing: text
 * ============================================================================ */
static void get_text_arg(duk_context* ctx, int idx, char* buf, int bufsz) {
    if (duk_get_type(ctx,idx)==DUK_TYPE_NUMBER)
        snprintf(buf, bufsz, "%g", duk_get_number(ctx,idx));
    else {
        const char* s=duk_get_string(ctx,idx);
        if(s) strncpy(buf,s,bufsz-1); else buf[0]='\0';
    }
}

static duk_ret_t js_fillText(duk_context* ctx) {
    if (duk_get_top(ctx)<3) return duk_error(ctx,DUK_ERR_TYPE_ERROR,"fillText: 3 args");
    char text[512]=""; get_text_arg(ctx,0,text,sizeof(text));
    double x=duk_get_number(ctx,1), y=duk_get_number(ctx,2);
    if (!text[0]) return 0;
    duk_push_this(ctx);
    duk_get_prop_string(ctx,-1,"font");
    int fs=parse_font_size(duk_get_string(ctx,-1));
    duk_pop(ctx);
    duk_get_prop_string(ctx,-1,"fillStyle");
    const char* color=duk_get_string(ctx,-1);
    uint8_t r=255,g=255,b=255,a=255;
    if(color) parse_color(color,&r,&g,&b,&a);
    duk_pop(ctx);
    duk_get_prop_string(ctx,-1,"textAlign");
    const char* align=duk_get_string(ctx,-1); char align_buf[16]="";
    if(align) strncpy(align_buf,align,15);
    duk_pop(ctx);
    duk_get_prop_string(ctx,-1,"textBaseline");
    const char* baseline=duk_get_string(ctx,-1); char base_buf[16]="";
    if(baseline) strncpy(base_buf,baseline,15);
    duk_pop_2(ctx);
    void* target=get_current_canvas_texture(ctx);
    g_R->fill_text(target,text,x,y,r,g,b,a,fs,align_buf,base_buf);
    return 0;
}

static duk_ret_t js_strokeText(duk_context* ctx) {
    if (duk_get_top(ctx)<3) return duk_error(ctx,DUK_ERR_TYPE_ERROR,"strokeText: 3 args");
    char text[512]=""; get_text_arg(ctx,0,text,sizeof(text));
    double x=duk_get_number(ctx,1), y=duk_get_number(ctx,2);
    if (!text[0]) return 0;
    duk_push_this(ctx);
    duk_get_prop_string(ctx,-1,"font");
    int fs=parse_font_size(duk_get_string(ctx,-1));
    duk_pop(ctx);
    duk_get_prop_string(ctx,-1,"strokeStyle");
    const char* color=duk_get_string(ctx,-1);
    uint8_t r=0,g=0,b=0,a=255;
    if(color) parse_color(color,&r,&g,&b,&a);
    duk_pop(ctx);
    duk_get_prop_string(ctx,-1,"lineWidth");
    int lw=(int)duk_get_number(ctx,-1);
    duk_pop_2(ctx);
    void* target=get_current_canvas_texture(ctx);
    g_R->stroke_text(target,text,x,y,r,g,b,a,fs,lw);
    return 0;
}

static duk_ret_t js_measureText(duk_context* ctx) {
    const char* text=duk_get_string(ctx,0);
    duk_push_object(ctx);
    duk_push_number(ctx, (double)g_R->measure_text(text ? text : "", 20));
    duk_put_prop_string(ctx,-2,"width");
    return 1;
}

/* ============================================================================
 * Drawing: strokeRect
 * ============================================================================ */
static duk_ret_t js_strokeRect(duk_context* ctx) {
    double x=duk_get_number(ctx,0), y=duk_get_number(ctx,1);
    double w=duk_get_number(ctx,2), h=duk_get_number(ctx,3);
    duk_push_this(ctx);
    duk_get_prop_string(ctx,-1,"strokeStyle");
    const char* color=duk_get_string(ctx,-1); duk_pop(ctx);
    duk_get_prop_string(ctx,-1,"lineWidth");
    int lw=(int)duk_get_number(ctx,-1); duk_pop_2(ctx);
    uint8_t r=0,g=0,b=0,a=255;
    if(color) parse_color(color,&r,&g,&b,&a);
    void* target=get_current_canvas_texture(ctx);
    g_R->stroke_rect(target,x,y,w,h,r,g,b,a,lw);
    return 0;
}

/* ============================================================================
 * Drawing: path operations
 * ============================================================================ */
static duk_ret_t js_beginPath(duk_context* ctx) {
    g_cs.has_rect_path=0; g_cs.path_point_count=0;
    g_cs.path_start_x=0; g_cs.path_start_y=0;
    return 0;
}
static duk_ret_t js_moveTo(duk_context* ctx) {
    double x=duk_get_number(ctx,0), y=duk_get_number(ctx,1);
    double tx=g_cs.m[0]*x+g_cs.m[2]*y+g_cs.m[4];
    double ty=g_cs.m[1]*x+g_cs.m[3]*y+g_cs.m[5];
    g_cs.path_x=tx; g_cs.path_y=ty;
    g_cs.has_rect_path=3;
    g_cs.user_x=x; g_cs.user_y=y;
    g_cs.path_start_x=tx; g_cs.path_start_y=ty;
    g_cs.path_point_count=0;
    if (g_cs.path_point_count<255) {
        g_cs.path_points[g_cs.path_point_count*2]=tx;
        g_cs.path_points[g_cs.path_point_count*2+1]=ty;
        g_cs.path_point_count++;
    }
    return 0;
}
static duk_ret_t js_lineTo(duk_context* ctx) {
    double x=duk_get_number(ctx,0), y=duk_get_number(ctx,1);
    double tx=g_cs.m[0]*x+g_cs.m[2]*y+g_cs.m[4];
    double ty=g_cs.m[1]*x+g_cs.m[3]*y+g_cs.m[5];
    duk_push_this(ctx);
    duk_get_prop_string(ctx,-1,"strokeStyle");
    const char* color=duk_get_string(ctx,-1); duk_pop_2(ctx);
    uint8_t r=0,g=0,b=0,a=255;
    if(color&&color[0]=='#') parse_css_hex(color,&r,&g,&b);
    void* target=get_current_canvas_texture(ctx);
    if (target && (g_cs.has_rect_path==3||g_cs.has_rect_path==4))
        g_R->draw_line(target,(int)g_cs.path_x,(int)g_cs.path_y,(int)tx,(int)ty,r,g,b,a);
    g_cs.path_x=tx; g_cs.path_y=ty;
    g_cs.user_x=x; g_cs.user_y=y;
    if (g_cs.path_point_count<255) {
        g_cs.path_points[g_cs.path_point_count*2]=tx;
        g_cs.path_points[g_cs.path_point_count*2+1]=ty;
        g_cs.path_point_count++;
    }
    return 0;
}
static duk_ret_t js_closePath(duk_context* ctx) {
    if (g_cs.path_point_count>1) {
        if (g_cs.path_point_count<255) {
            g_cs.path_points[g_cs.path_point_count*2]=g_cs.path_start_x;
            g_cs.path_points[g_cs.path_point_count*2+1]=g_cs.path_start_y;
            g_cs.path_point_count++;
        }
        g_cs.path_x=g_cs.path_start_x;
        g_cs.path_y=g_cs.path_start_y;
    }
    return 0;
}

static duk_ret_t js_fill(duk_context* ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx,-1,"fillStyle");
    const char* color=duk_get_string(ctx,-1); duk_pop(ctx);
    uint8_t r=0,g=0,b=0,a=255;
    if(color) parse_color(color,&r,&g,&b,&a);
    duk_get_prop_string(ctx,-1,"globalAlpha");
    double ga=duk_get_number(ctx,-1); duk_pop_2(ctx);
    a=(uint8_t)(a*ga);
    int use_lighter=get_global_composite(ctx);
    void* target=get_current_canvas_texture(ctx);
    if (!target) return 0;
    if (g_cs.has_rect_path==1) {
        g_R->fill_rect(target,(int)g_cs.path_x,(int)g_cs.path_y,
                       (int)g_cs.path_w,(int)g_cs.path_h,
                       r,g,b,a,use_lighter,g_cs.m);
    } else if (g_cs.has_rect_path==2) {
        g_R->fill_circle(target,g_cs.path_x,g_cs.path_y,
                         (int)g_cs.path_w,r,g,b,a,use_lighter);
    } else if (g_cs.has_rect_path==3||g_cs.has_rect_path==4) {
        if (g_cs.path_point_count>2)
            g_R->fill_polygon(target,g_cs.path_points,g_cs.path_point_count,
                              r,g,b,a,use_lighter);
    }
    return 0;
}

static duk_ret_t js_stroke(duk_context* ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx,-1,"strokeStyle");
    const char* color=duk_get_string(ctx,-1); duk_pop_2(ctx);
    uint8_t r=0,g=0,b=0,a=255;
    if(color) parse_color(color,&r,&g,&b,&a);
    void* target=get_current_canvas_texture(ctx);
    if (!target) return 0;
    if (g_cs.has_rect_path==1) {
        g_R->stroke_rect(target,g_cs.path_x,g_cs.path_y,
                         g_cs.path_w,g_cs.path_h,r,g,b,a,1);
    } else if (g_cs.has_rect_path==2) {
        g_R->stroke_circle(target,g_cs.path_x,g_cs.path_y,(int)g_cs.path_w,r,g,b,a);
    }
    /* has_rect_path==3/4: lines already drawn by lineTo */
    return 0;
}

static duk_ret_t js_clip(duk_context* ctx) {
    if (g_cs.has_rect_path==1) {
        g_cs.clip_x=(int)g_cs.path_x; g_cs.clip_y=(int)g_cs.path_y;
        g_cs.clip_w=(int)g_cs.path_w; g_cs.clip_h=(int)g_cs.path_h;
        g_cs.has_clip=1; g_cs.clip_type=1;
        g_R->set_clip_rect(g_R->get_main_texture(),
                           g_cs.clip_x,g_cs.clip_y,g_cs.clip_w,g_cs.clip_h);
    } else if (g_cs.has_rect_path==2) {
        int radius=(int)g_cs.path_w;
        g_cs.clip_x=(int)g_cs.path_x-radius; g_cs.clip_y=(int)g_cs.path_y-radius;
        g_cs.clip_w=radius*2; g_cs.clip_h=radius*2;
        g_cs.has_clip=1; g_cs.clip_type=2;
        g_R->set_clip_rect(g_R->get_main_texture(),
                           g_cs.clip_x,g_cs.clip_y,g_cs.clip_w,g_cs.clip_h);
    }
    return 0;
}

static duk_ret_t js_bezierCurveTo(duk_context* ctx) {
    double cp1x=duk_get_number(ctx,0),cp1y=duk_get_number(ctx,1);
    double cp2x=duk_get_number(ctx,2),cp2y=duk_get_number(ctx,3);
    double x=duk_get_number(ctx,4),y=duk_get_number(ctx,5);
    for (int i=1;i<=20;i++) {
        double t=(double)i/20, t2=t*t, t3=t2*t;
        double mt=1-t, mt2=mt*mt, mt3=mt2*mt;
        double ux=mt3*g_cs.user_x+3*mt2*t*cp1x+3*mt*t2*cp2x+t3*x;
        double uy=mt3*g_cs.user_y+3*mt2*t*cp1y+3*mt*t2*cp2y+t3*y;
        double tx=g_cs.m[0]*ux+g_cs.m[2]*uy+g_cs.m[4];
        double ty=g_cs.m[1]*ux+g_cs.m[3]*uy+g_cs.m[5];
        if (g_cs.path_point_count<255) {
            g_cs.path_points[g_cs.path_point_count*2]=tx;
            g_cs.path_points[g_cs.path_point_count*2+1]=ty;
            g_cs.path_point_count++;
        }
    }
    g_cs.path_x=g_cs.m[0]*x+g_cs.m[2]*y+g_cs.m[4];
    g_cs.path_y=g_cs.m[1]*x+g_cs.m[3]*y+g_cs.m[5];
    g_cs.user_x=x; g_cs.user_y=y;
    g_cs.has_rect_path=4;
    return 0;
}

static duk_ret_t js_rect(duk_context* ctx) {
    g_cs.path_x=duk_get_number(ctx,0); g_cs.path_y=duk_get_number(ctx,1);
    g_cs.path_w=duk_get_number(ctx,2); g_cs.path_h=duk_get_number(ctx,3);
    g_cs.has_rect_path=1;
    return 0;
}

static duk_ret_t js_arc(duk_context* ctx) {
    double x=duk_get_number(ctx,0), y=duk_get_number(ctx,1);
    double radius=duk_get_number(ctx,2);
    double startAngle=duk_get_number(ctx,3), endAngle=duk_get_number(ctx,4);
    int ccw=duk_get_boolean(ctx,5);
    g_cs.path_x=x; g_cs.path_y=y; g_cs.path_w=radius;
    g_cs.path_h=startAngle; g_cs.has_rect_path=2;
    duk_push_this(ctx);
    duk_get_prop_string(ctx,-1,"strokeStyle");
    const char* color=duk_get_string(ctx,-1); duk_pop_2(ctx);
    uint8_t r=0,g=0,b=0,a=255;
    if(color&&color[0]=='#') parse_css_hex(color,&r,&g,&b);
    void* target=get_current_canvas_texture(ctx);
    if (target)
        g_R->draw_arc_points(target,x,y,radius,startAngle,endAngle,ccw,r,g,b,a);
    return 0;
}

static duk_ret_t js_arcTo(duk_context* ctx)           { return 0; }
static duk_ret_t js_quadraticCurveTo(duk_context* ctx){ return 0; }

static duk_ret_t js_isPointInPath(duk_context* ctx) {
    double x=duk_get_number(ctx,0), y=duk_get_number(ctx,1);
    if (g_cs.has_rect_path &&
        x>=g_cs.path_x && x<=g_cs.path_x+g_cs.path_w &&
        y>=g_cs.path_y && y<=g_cs.path_y+g_cs.path_h) {
        duk_push_true(ctx); return 1;
    }
    duk_push_false(ctx); return 1;
}

/* ============================================================================
 * Drawing: gradients / patterns
 * ============================================================================ */
static duk_ret_t js_gradient_addColorStop(duk_context* ctx) {
    double offset=duk_get_number(ctx,0);
    const char* color=duk_require_string(ctx,1);
    duk_push_this(ctx);
    char key[32];
    snprintf(key,sizeof(key),"stop_%d",(int)(offset*1000));
    duk_push_string(ctx,color); duk_put_prop_string(ctx,-2,key);
    snprintf(key,sizeof(key),"offset_%d",(int)(offset*1000));
    duk_push_number(ctx,offset); duk_put_prop_string(ctx,-2,key);
    duk_pop(ctx);
    return 0;
}
static duk_ret_t js_createLinearGradient(duk_context* ctx) {
    duk_push_object(ctx);
    duk_push_c_function(ctx,js_gradient_addColorStop,2);
    duk_put_prop_string(ctx,-2,"addColorStop");
    return 1;
}
static duk_ret_t js_createRadialGradient(duk_context* ctx) {
    return js_createLinearGradient(ctx);
}
static duk_ret_t js_createPattern(duk_context* ctx) {
    if (duk_get_top(ctx)<2)
        return duk_error(ctx,DUK_ERR_TYPE_ERROR,"createPattern: 2 args");
    duk_dup(ctx,0);
    duk_get_prop_string(ctx,-1,"\xFF""ptr");
    MyImage* img=(MyImage*)duk_get_pointer(ctx,-1);
    duk_pop_2(ctx);
    if (!img) return duk_error(ctx,DUK_ERR_TYPE_ERROR,"createPattern: need Image");
    const char* rep=duk_get_string(ctx,1); if(!rep) rep="repeat";
    duk_push_object(ctx);
    duk_push_pointer(ctx,(void*)img);
    duk_put_prop_string(ctx,-2,"\xFF""image");
    duk_push_string(ctx,rep);
    duk_put_prop_string(ctx,-2,"repetition");
    return 1;
}

/* ============================================================================
 * Drawing: toDataURL
 * ============================================================================ */
static duk_ret_t js_toDataURL(duk_context* ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx,-1,"\xFF""canvasPtr");
    MyCanvas* canvas=(MyCanvas*)duk_get_pointer(ctx,-1);
    duk_pop_2(ctx);
    if (!canvas||!canvas->texture||canvas->width<=0||canvas->height<=0) {
        duk_push_string(ctx,"data:image/png;base64,"); return 1;
    }
    char* url=g_R->to_data_url(canvas->texture,canvas->width,canvas->height);
    if (url) { duk_push_string(ctx,url); free(url); }
    else     duk_push_string(ctx,"data:image/png;base64,");
    return 1;
}

/* ============================================================================
 * getContext (returns 2D context object attached to this canvas)
 * ============================================================================ */
static duk_ret_t js_getContext(duk_context* ctx) {
    const char* mode=duk_require_string(ctx,0);
    if (strcmp(mode,"2d")!=0)
        return duk_error(ctx,DUK_ERR_TYPE_ERROR,"Only '2d' context supported");
    duk_push_this(ctx);
    duk_get_prop_string(ctx,-1,"\xFF""canvasPtr");
    MyCanvas* canvas=(MyCanvas*)duk_get_pointer(ctx,-1);
    duk_pop_2(ctx);
    duk_push_object(ctx);
    duk_push_pointer(ctx,(void*)canvas);
    duk_put_prop_string(ctx,-2,"\xFF""canvasPtr");

#define PUT_FN(name,fn,argc) \
    duk_push_c_function(ctx,fn,argc); duk_put_prop_string(ctx,-2,name)
#define PUT_NUM(name,val) \
    duk_push_number(ctx,val); duk_put_prop_string(ctx,-2,name)
#define PUT_STR(name,val) \
    duk_push_string(ctx,val); duk_put_prop_string(ctx,-2,name)
#define PUT_BOOL(name,val) \
    duk_push_boolean(ctx,val); duk_put_prop_string(ctx,-2,name)

    PUT_FN("drawImage",          js_drawImage,          DUK_VARARGS);
    PUT_FN("fillRect",           js_fillRect,           4);
    PUT_FN("clearRect",          js_clearRect,          4);
    PUT_FN("getImageData",       js_getImageData,       4);
    PUT_FN("getImageDataHD",     js_getImageDataHD,     4);
    PUT_FN("putImageData",       js_putImageData,       3);
    PUT_FN("createImageData",    js_createImageData,    2);
    PUT_FN("save",               js_save,               0);
    PUT_FN("restore",            js_restore,            0);
    PUT_FN("scale",              js_scale,              2);
    PUT_FN("translate",          js_translate,          2);
    PUT_FN("rotate",             js_rotate,             1);
    PUT_FN("setTransform",       js_setTransform,       6);
    PUT_FN("transform",          js_transform,          6);
    PUT_FN("fillText",           js_fillText,           3);
    PUT_FN("strokeText",         js_strokeText,         3);
    PUT_FN("measureText",        js_measureText,        1);
    PUT_FN("strokeRect",         js_strokeRect,         4);
    PUT_FN("beginPath",          js_beginPath,          0);
    PUT_FN("moveTo",             js_moveTo,             2);
    PUT_FN("lineTo",             js_lineTo,             2);
    PUT_FN("closePath",          js_closePath,          0);
    PUT_FN("fill",               js_fill,               0);
    PUT_FN("stroke",             js_stroke,             0);
    PUT_FN("clip",               js_clip,               0);
    PUT_FN("bezierCurveTo",      js_bezierCurveTo,      6);
    PUT_FN("rect",               js_rect,               4);
    PUT_FN("arc",                js_arc,                6);
    PUT_FN("arcTo",              js_arcTo,              5);
    PUT_FN("quadraticCurveTo",   js_quadraticCurveTo,   4);
    PUT_FN("createLinearGradient",js_createLinearGradient,4);
    PUT_FN("createRadialGradient",js_createRadialGradient,6);
    PUT_FN("createPattern",      js_createPattern,      2);
    PUT_FN("isPointInPath",      js_isPointInPath,      2);
    PUT_STR("fillStyle",         "#000000");
    PUT_STR("strokeStyle",       "#000000");
    PUT_NUM("lineWidth",         1.0);
    PUT_NUM("globalAlpha",       1.0);
    PUT_NUM("backingStorePixelRatio",1.0);
    PUT_BOOL("imageSmoothingEnabled",0);
    PUT_STR("lineCap",           "butt");
    PUT_STR("lineJoin",          "miter");
    PUT_NUM("miterLimit",        10.0);
    PUT_NUM("shadowOffsetX",     0);
    PUT_NUM("shadowOffsetY",     0);
    PUT_NUM("shadowBlur",        0);
    PUT_STR("shadowColor",       "rgba(0, 0, 0, 0)");
    PUT_STR("globalCompositeOperation","source-over");
    PUT_STR("font",              "10px sans-serif");
    PUT_STR("textAlign",         "start");
    PUT_STR("textBaseline",      "alphabetic");
#undef PUT_FN
#undef PUT_NUM
#undef PUT_STR
#undef PUT_BOOL
    return 1;
}

/* ============================================================================
 * Canvas element helpers
 * ============================================================================ */
static void attach_canvas_element(duk_context* ctx, MyCanvas* c) {
    duk_push_pointer(ctx,(void*)c);
    duk_put_prop_string(ctx,-2,"\xFF""canvasPtr");
    duk_push_string(ctx,"width");
    duk_push_c_function(ctx,js_canvas_width_getter,0);
    duk_push_c_function(ctx,js_canvas_width_setter,1);
    duk_def_prop(ctx,-4,DUK_DEFPROP_HAVE_GETTER|DUK_DEFPROP_HAVE_SETTER);
    duk_push_string(ctx,"height");
    duk_push_c_function(ctx,js_canvas_height_getter,0);
    duk_push_c_function(ctx,js_canvas_height_setter,1);
    duk_def_prop(ctx,-4,DUK_DEFPROP_HAVE_GETTER|DUK_DEFPROP_HAVE_SETTER);
    duk_push_c_function(ctx,js_getContext,1);
    duk_put_prop_string(ctx,-2,"getContext");
    duk_push_object(ctx);
    duk_push_string(ctx,""); duk_put_prop_string(ctx,-2,"imageRendering");
    duk_put_prop_string(ctx,-2,"style");
    duk_push_int(ctx,c->width);  duk_put_prop_string(ctx,-2,"clientWidth");
    duk_push_int(ctx,c->height); duk_put_prop_string(ctx,-2,"clientHeight");
    duk_push_int(ctx,c->width);  duk_put_prop_string(ctx,-2,"offsetWidth");
    duk_push_int(ctx,c->height); duk_put_prop_string(ctx,-2,"offsetHeight");
    duk_push_c_function(ctx,js_toDataURL,0);
    duk_put_prop_string(ctx,-2,"toDataURL");
}

/* ============================================================================
 * document.getElementById / getElementsByTagName / createElement
 * ============================================================================ */
static duk_ret_t js_getElementById(duk_context* ctx) {
    const char* id=duk_require_string(ctx,0);
    for (int i=0;i<g_canvas_count;i++) {
        if (!strcmp(id,g_canvas_info[i].id)) {
            duk_push_object(ctx);
            MyCanvas* c=(MyCanvas*)calloc(1,sizeof(MyCanvas));
            if (!c) return duk_error(ctx,DUK_ERR_ERROR,"OOM MyCanvas");
            c->width  = g_canvas_info[i].width >0 ? g_canvas_info[i].width  : g_win_w;
            c->height = g_canvas_info[i].height>0 ? g_canvas_info[i].height : g_win_h;
            if (i==0) c->texture = g_R->get_main_texture();
            else {
                c->texture = g_R->create_texture(c->width, c->height);
            }
            attach_canvas_element(ctx, c);
            return 1;
        }
    }
    for (int i=0;i<g_image_count;i++) {
        if (!strcmp(id,g_image_info[i].id) && g_image_info[i].id[0]!='\0') {
            duk_get_global_string(ctx,"document");
            duk_get_prop_string(ctx,-1,"images");
            duk_get_prop_index(ctx,-1,i);
            duk_remove(ctx,-2); duk_remove(ctx,-2);
            if (duk_is_object(ctx,-1)) return 1;
        }
    }
    duk_push_object(ctx);
    duk_push_string(ctx,""); duk_put_prop_string(ctx,-2,"innerHTML");
    duk_push_string(ctx,""); duk_put_prop_string(ctx,-2,"textContent");
    return 1;
}

static duk_ret_t js_getElementsByTagName(duk_context* ctx) {
    const char* tag=duk_require_string(ctx,0);
    duk_push_array(ctx);
    if (strcmp(tag,"canvas")==0) {
        for (int i=0;i<g_canvas_count&&i<10;i++) {
            duk_push_object(ctx);
            MyCanvas* c=(MyCanvas*)calloc(1,sizeof(MyCanvas));
            if (!c) { duk_pop(ctx); continue; }
            c->width  = g_canvas_info[i].width >0 ? g_canvas_info[i].width  : g_win_w;
            c->height = g_canvas_info[i].height>0 ? g_canvas_info[i].height : g_win_h;
            attach_canvas_element(ctx, c);
            duk_put_prop_index(ctx,-2,i);
        }
    }
    return 1;
}

static duk_ret_t js_createElement(duk_context* ctx) {
    const char* tag=duk_require_string(ctx,0);
    if (strcmp(tag,"canvas")==0) {
        duk_push_object(ctx);
        MyCanvas* c=(MyCanvas*)calloc(1,sizeof(MyCanvas));
        if (!c) return duk_error(ctx,DUK_ERR_ERROR,"OOM MyCanvas");
        c->width=300; c->height=150;
        c->texture=g_R->create_texture(c->width,c->height);
        attach_canvas_element(ctx, c);
        return 1;
    }
    duk_push_object(ctx);
    return 1;
}

/* ============================================================================
 * Timer management
 * ============================================================================ */
static double nowMs(void) { return g_R->get_time_ms(); }

static duk_ret_t js_setTimeout(duk_context* ctx) {
    duk_require_callable(ctx,0);
    double ms=duk_require_number(ctx,1);
    int fid=store_func(ctx,0);
    for (int i=0;i<MAX_INTERVALS;i++) {
        if (!g_intervals[i].active) {
            g_intervals[i].active=1;
            g_intervals[i].intervalMs=-ms;
            g_intervals[i].nextTime=nowMs()+ms;
            g_intervals[i].funcId=fid;
            duk_push_int(ctx,i); return 1;
        }
    }
    duk_push_int(ctx,-1); return 1;
}

static duk_ret_t js_clearTimeout(duk_context* ctx) {
    int id=duk_require_int(ctx,0);
    if (id>=0&&id<MAX_INTERVALS) g_intervals[id].active=0;
    return 0;
}

static duk_ret_t js_setInterval(duk_context* ctx) {
    duk_require_callable(ctx,0);
    double ms=duk_require_number(ctx,1);
    int fid=store_func(ctx,0);
    for (int i=0;i<MAX_INTERVALS;i++) {
        if (!g_intervals[i].active) {
            g_intervals[i].active=1;
            g_intervals[i].intervalMs=ms;
            g_intervals[i].nextTime=nowMs()+ms;
            g_intervals[i].funcId=fid;
            duk_push_int(ctx,i); return 1;
        }
    }
    duk_push_int(ctx,-1); return 1;
}

static duk_ret_t js_requestAnimationFrame(duk_context* ctx) {
    duk_require_callable(ctx,0);
    int fid=store_func(ctx,0);
    for (int i=0;i<MAX_INTERVALS;i++) {
        if (!g_intervals[i].active) {
            g_intervals[i].active=1;
            g_intervals[i].intervalMs=-16.67;
            g_intervals[i].nextTime=nowMs()+16.67;
            g_intervals[i].funcId=fid;
            duk_push_int(ctx,i); return 1;
        }
    }
    duk_push_int(ctx,-1); return 1;
}

static duk_ret_t js_cancelAnimationFrame(duk_context* ctx) {
    int id=duk_require_int(ctx,0);
    if (id>=0&&id<MAX_INTERVALS) g_intervals[id].active=0;
    return 0;
}

/* ============================================================================
 * Window / document event listeners
 * ============================================================================ */
static duk_ret_t js_noop(duk_context* ctx) { (void)ctx; return 0; }

static duk_ret_t js_doc_addEventListener(duk_context* ctx) {
    const char* ev=duk_require_string(ctx,0);
    duk_require_callable(ctx,1);
    if (strcmp(ev,"DOMContentLoaded")==0||strcmp(ev,"load")==0) {
        duk_dup(ctx,1);
        duk_get_global_string(ctx,"window");
        if (duk_pcall_method(ctx,0)!=0)
            fprintf(stderr,"[doc_addEventListener] %s err: %s\n",
                    ev,duk_safe_to_string(ctx,-1));
        duk_pop(ctx);
    }
    return 0;
}

static duk_ret_t js_win_addEventListener(duk_context* ctx) {
    const char* ev=duk_require_string(ctx,0);
    duk_require_callable(ctx,1);
    if      (strcmp(ev,"load")==0)
        g_window_load_funcId=store_func(ctx,1);
    else if (strcmp(ev,"keydown")==0 && g_keydown_listener_count<MAX_KEY_LISTENERS)
        g_keydown_listeners[g_keydown_listener_count++]=store_func(ctx,1);
    else if (strcmp(ev,"keyup")==0 && g_keyup_listener_count<MAX_KEY_LISTENERS)
        g_keyup_listeners[g_keyup_listener_count++]=store_func(ctx,1);
    return 0;
}

static duk_ret_t js_onload_set(duk_context* ctx) {
    duk_require_callable(ctx,0);
    g_window_onload_funcId=store_func(ctx,0);
    return 0;
}
static duk_ret_t js_onload_get(duk_context* ctx) {
    if (g_window_onload_funcId<0) duk_push_undefined(ctx);
    else push_stored_func(ctx,g_window_onload_funcId);
    return 1;
}

/* ============================================================================
 * Audio (stubs)
 * ============================================================================ */
static duk_ret_t js_audio_load(duk_context* ctx) {
    duk_push_this(ctx); return 1;
}
static duk_ret_t js_audio_play(duk_context* ctx) {
    duk_push_this(ctx);
    duk_push_false(ctx); duk_put_prop_string(ctx,-2,"paused");
    return 1;
}
static duk_ret_t js_audio_pause(duk_context* ctx) {
    duk_push_this(ctx);
    duk_push_true(ctx); duk_put_prop_string(ctx,-2,"paused");
    return 1;
}
static duk_ret_t js_audio_canPlayType(duk_context* ctx) {
    const char* t=duk_get_string(ctx,0);
    if (t&&(strstr(t,"ogg")||strstr(t,"mp3")||strstr(t,"wav")||strstr(t,"mpeg")))
        duk_push_string(ctx,"maybe");
    else duk_push_string(ctx,"");
    return 1;
}
static duk_ret_t js_audio_addEventListener(duk_context* ctx) {
    const char* ev=duk_get_string(ctx,0);
    if (duk_is_callable(ctx,1) &&
        (strcmp(ev,"canplaythrough")==0||strcmp(ev,"loadeddata")==0)) {
        duk_dup(ctx,1); duk_push_this(ctx);
        if (duk_pcall(ctx,1)!=0)
            fprintf(stderr,"[Audio] event err: %s\n",duk_safe_to_string(ctx,-1));
        duk_pop(ctx);
    }
    return 0;
}
static duk_ret_t js_html5_audio(duk_context* ctx) {
    const char* src=duk_get_string(ctx,0);
    duk_push_this(ctx);
    if (src) { duk_push_string(ctx,src); duk_put_prop_string(ctx,-2,"src"); }
    duk_push_c_function(ctx,js_audio_load,0);        duk_put_prop_string(ctx,-2,"load");
    duk_push_c_function(ctx,js_audio_play,0);        duk_put_prop_string(ctx,-2,"play");
    duk_push_c_function(ctx,js_audio_pause,0);       duk_put_prop_string(ctx,-2,"pause");
    duk_push_c_function(ctx,js_audio_canPlayType,1); duk_put_prop_string(ctx,-2,"canPlayType");
    duk_push_c_function(ctx,js_audio_addEventListener,2); duk_put_prop_string(ctx,-2,"addEventListener");
    duk_push_c_function(ctx,js_noop,DUK_VARARGS);   duk_put_prop_string(ctx,-2,"removeEventListener");
    duk_push_false(ctx); duk_put_prop_string(ctx,-2,"loop");
    duk_push_number(ctx,1.0); duk_put_prop_string(ctx,-2,"volume");
    duk_push_true(ctx);  duk_put_prop_string(ctx,-2,"paused");
    duk_push_false(ctx); duk_put_prop_string(ctx,-2,"ended");
    duk_push_number(ctx,0); duk_put_prop_string(ctx,-2,"currentTime");
    duk_push_number(ctx,0); duk_put_prop_string(ctx,-2,"duration");
    return 0;
}
static duk_ret_t js_html_element_ctor(duk_context* ctx) {
    duk_push_this(ctx); return 0;
}

/* ============================================================================
 * localStorage
 * ============================================================================ */
static duk_ret_t js_ls_setItem(duk_context* ctx) {
    const char* key=duk_require_string(ctx,0);
    const char* val=duk_require_string(ctx,1);
    for (int i=0;i<g_storage_count;i++) {
        if (!strcmp(g_storage[i].key,key)) {
            strncpy(g_storage[i].value,val,sizeof(g_storage[i].value)-1); return 0;
        }
    }
    if (g_storage_count<MAX_STORAGE_ITEMS) {
        strncpy(g_storage[g_storage_count].key,  key,sizeof(g_storage[g_storage_count].key)-1);
        strncpy(g_storage[g_storage_count].value,val,sizeof(g_storage[g_storage_count].value)-1);
        g_storage_count++;
    }
    return 0;
}
static duk_ret_t js_ls_getItem(duk_context* ctx) {
    const char* key=duk_require_string(ctx,0);
    for (int i=0;i<g_storage_count;i++)
        if (!strcmp(g_storage[i].key,key)) { duk_push_string(ctx,g_storage[i].value); return 1; }
    duk_push_null(ctx); return 1;
}
static duk_ret_t js_ls_removeItem(duk_context* ctx) {
    const char* key=duk_require_string(ctx,0);
    for (int i=0;i<g_storage_count;i++) {
        if (!strcmp(g_storage[i].key,key)) {
            for (int j=i;j<g_storage_count-1;j++) g_storage[j]=g_storage[j+1];
            g_storage_count--; break;
        }
    }
    return 0;
}
static duk_ret_t js_ls_clear(duk_context* ctx) { g_storage_count=0; return 0; }

/* ============================================================================
 * alert
 * ============================================================================ */
static duk_ret_t js_alert(duk_context* ctx) {
    printf("[alert] %s\n", duk_require_string(ctx,0)); return 0;
}

/* ============================================================================
 * Global object setup helpers
 * ============================================================================ */
static void create_window_obj(duk_context* ctx) {
    duk_push_global_object(ctx);
    duk_push_c_function(ctx,js_setInterval,2);  duk_put_prop_string(ctx,-2,"setInterval");
    duk_push_c_function(ctx,js_setTimeout,2);   duk_put_prop_string(ctx,-2,"setTimeout");
    duk_push_c_function(ctx,js_clearTimeout,1); duk_put_prop_string(ctx,-2,"clearTimeout");
    duk_push_c_function(ctx,js_clearTimeout,1); duk_put_prop_string(ctx,-2,"clearInterval");
    duk_push_c_function(ctx,js_requestAnimationFrame,1);
    duk_put_prop_string(ctx,-2,"requestAnimationFrame");
    duk_push_c_function(ctx,js_cancelAnimationFrame,1);
    duk_put_prop_string(ctx,-2,"cancelAnimationFrame");
    duk_push_c_function(ctx,js_win_addEventListener,2);
    duk_put_prop_string(ctx,-2,"addEventListener");
    duk_push_string(ctx,"onload");
    duk_push_c_function(ctx,js_onload_get,0);
    duk_push_c_function(ctx,js_onload_set,1);
    duk_def_prop(ctx,-4,DUK_DEFPROP_HAVE_GETTER|DUK_DEFPROP_HAVE_SETTER);
    duk_push_number(ctx,1.0);  duk_put_prop_string(ctx,-2,"devicePixelRatio");
    duk_push_int(ctx,g_win_w); duk_put_prop_string(ctx,-2,"innerWidth");
    duk_push_int(ctx,g_win_h); duk_put_prop_string(ctx,-2,"innerHeight");
    duk_dup(ctx,-1); duk_put_prop_string(ctx,-2,"window");
    duk_put_global_string(ctx,"window");
}

static void create_screen_obj(duk_context* ctx) {
    duk_push_object(ctx);
    duk_push_int(ctx,g_win_w); duk_put_prop_string(ctx,-2,"availWidth");
    duk_push_int(ctx,g_win_h); duk_put_prop_string(ctx,-2,"availHeight");
    duk_push_int(ctx,g_win_w); duk_put_prop_string(ctx,-2,"width");
    duk_push_int(ctx,g_win_h); duk_put_prop_string(ctx,-2,"height");
    duk_put_global_string(ctx,"screen");
    duk_get_global_string(ctx,"window");
    duk_push_object(ctx);
    duk_push_int(ctx,g_win_w); duk_put_prop_string(ctx,-2,"availWidth");
    duk_push_int(ctx,g_win_h); duk_put_prop_string(ctx,-2,"availHeight");
    duk_push_int(ctx,g_win_w); duk_put_prop_string(ctx,-2,"width");
    duk_push_int(ctx,g_win_h); duk_put_prop_string(ctx,-2,"height");
    duk_put_prop_string(ctx,-2,"screen");
    duk_pop(ctx);
}

static void create_navigator_obj(duk_context* ctx) {
    duk_push_object(ctx);
    duk_push_string(ctx,"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
    duk_put_prop_string(ctx,-2,"userAgent");
    duk_push_string(ctx,"Netscape"); duk_put_prop_string(ctx,-2,"appName");
    duk_push_string(ctx,"5.0");      duk_put_prop_string(ctx,-2,"appVersion");
    duk_push_string(ctx,"Linux x86_64"); duk_put_prop_string(ctx,-2,"platform");
    duk_put_global_string(ctx,"navigator");
}

static void create_document(duk_context* ctx) {
    duk_push_object(ctx);
    duk_push_c_function(ctx,js_getElementById,1);     duk_put_prop_string(ctx,-2,"getElementById");
    duk_push_c_function(ctx,js_getElementsByTagName,1);duk_put_prop_string(ctx,-2,"getElementsByTagName");
    duk_push_c_function(ctx,js_createElement,1);      duk_put_prop_string(ctx,-2,"createElement");
    duk_push_c_function(ctx,js_doc_addEventListener,2);duk_put_prop_string(ctx,-2,"addEventListener");
    duk_push_string(ctx,"complete"); duk_put_prop_string(ctx,-2,"readyState");
    duk_push_object(ctx);
    duk_push_string(ctx,"file:///");
    duk_put_prop_string(ctx,-2,"href");
    duk_put_prop_string(ctx,-2,"location");
    duk_push_object(ctx); duk_put_prop_string(ctx,-2,"body");
    duk_push_object(ctx); duk_put_prop_string(ctx,-2,"head");

    duk_push_array(ctx);
    int total = g_image_count + 4;
    for (int i=0;i<total;i++) {
        duk_push_global_object(ctx);
        duk_get_prop_string(ctx,-1,"Image");
        if (duk_is_undefined(ctx,-1)) { duk_pop(ctx); duk_push_undefined(ctx); }
        else duk_new(ctx,0);
        duk_remove(ctx,-2);
        MyImage* im=(MyImage*)calloc(1,sizeof(MyImage));
        if (!im) { duk_push_error_object(ctx,DUK_ERR_ERROR,"OOM"); duk_throw(ctx); }
        duk_push_pointer(ctx,(void*)im);
        duk_put_prop_string(ctx,-2,"\xFF""ptr");
        duk_put_prop_index(ctx,-2,i);
    }
    duk_put_prop_string(ctx,-2,"images");
    duk_put_global_string(ctx,"document");
}

static void create_localStorage(duk_context* ctx) {
    duk_push_object(ctx);
    duk_push_c_function(ctx,js_ls_setItem,2);    duk_put_prop_string(ctx,-2,"setItem");
    duk_push_c_function(ctx,js_ls_getItem,1);    duk_put_prop_string(ctx,-2,"getItem");
    duk_push_c_function(ctx,js_ls_removeItem,1); duk_put_prop_string(ctx,-2,"removeItem");
    duk_push_c_function(ctx,js_ls_clear,0);      duk_put_prop_string(ctx,-2,"clear");
    duk_put_global_string(ctx,"localStorage");
}

static void create_audio_obj(duk_context* ctx) {
    duk_push_c_function(ctx,js_html5_audio,1); duk_put_global_string(ctx,"Audio");
    duk_push_c_function(ctx,js_html_element_ctor,0);
    duk_push_object(ctx); duk_put_prop_string(ctx,-2,"prototype");
    duk_put_global_string(ctx,"HTMLElement");
}

/* ============================================================================
 * Key event dispatch
 * ============================================================================ */
static void dispatch_key_event(duk_context* ctx, const char* evtype,
                                int keycode, int* listeners, int count) {
    if (!count) return;
    duk_push_object(ctx);
    duk_push_string(ctx,evtype); duk_put_prop_string(ctx,-2,"type");
    duk_push_int(ctx,keycode);   duk_put_prop_string(ctx,-2,"keyCode");
    duk_push_int(ctx,keycode);   duk_put_prop_string(ctx,-2,"which");
    duk_push_object(ctx);
    duk_push_string(ctx,"BODY"); duk_put_prop_string(ctx,-2,"tagName");
    duk_put_prop_string(ctx,-2,"target");
    duk_push_c_function(ctx,js_noop,0); duk_put_prop_string(ctx,-2,"preventDefault");
    duk_push_c_function(ctx,js_noop,0); duk_put_prop_string(ctx,-2,"stopPropagation");
    duk_idx_t evt=duk_get_top_index(ctx);
    for (int i=0;i<count;i++) {
        push_stored_func(ctx,listeners[i]);
        if (!duk_is_callable(ctx,-1)) { duk_pop(ctx); continue; }
        duk_dup(ctx,evt);
        if (duk_pcall(ctx,1)!=0)
            fprintf(stderr,"[key_event] %s err: %s\n",evtype,duk_safe_to_string(ctx,-1));
        duk_pop(ctx);
    }
    duk_pop(ctx);
}

/* ============================================================================
 * Timer tick
 * ============================================================================ */
static void check_timers(duk_context* ctx) {
    double t=nowMs();
    for (int i=0;i<MAX_INTERVALS;i++) {
        if (!g_intervals[i].active) continue;
        if (t >= g_intervals[i].nextTime) {
            int isTimeout=(g_intervals[i].intervalMs<0);
            double absMs=isTimeout ? -g_intervals[i].intervalMs : g_intervals[i].intervalMs;
            if (!isTimeout) g_intervals[i].nextTime+=absMs;
            else            g_intervals[i].active=0;
            push_stored_func(ctx,g_intervals[i].funcId);
            if (duk_is_callable(ctx,-1)) {
                duk_get_global_string(ctx,"window");
                if (duk_pcall_method(ctx,0)!=0)
                    fprintf(stderr,"[timer] err: %s\n",duk_safe_to_string(ctx,-1));
                duk_pop(ctx);
            } else duk_pop(ctx);
        }
    }
}

/* ============================================================================
 * JSCoreInterface implementations
 * ============================================================================ */
static int jsi_init(RendererInterface* renderer,
                    InputInterface* input,
                    SoundInterface* sound) {
    g_R   = renderer;
    g_IN  = input;
    g_SND = sound;
    g_ctx = duk_create_heap_default();
    if (!g_ctx) { fprintf(stderr,"duk_create_heap failed\n"); return 0; }
    duk_console_init(g_ctx, DUK_CONSOLE_PROXY_WRAPPER | DUK_CONSOLE_FLUSH);
    /* Init canvas transform to identity */
    g_cs.m[0]=1; g_cs.m[1]=0; g_cs.m[2]=0;
    g_cs.m[3]=1; g_cs.m[4]=0; g_cs.m[5]=0;
    return 1;
}

static void jsi_quit(void) {
    if (!g_ctx) return;
    /* Free images stored in document.images */
    duk_push_global_object(g_ctx);
    duk_get_prop_string(g_ctx,-1,"document");
    if (duk_is_object(g_ctx,-1)) {
        duk_get_prop_string(g_ctx,-1,"images");
        if (duk_is_array(g_ctx,-1)) {
            duk_uarridx_t len=(duk_uarridx_t)duk_get_length(g_ctx,-1);
            for (duk_uarridx_t i=0;i<len;i++) {
                duk_get_prop_index(g_ctx,-1,i);
                if (duk_is_object(g_ctx,-1)) {
                    duk_get_prop_string(g_ctx,-1,"\xFF""ptr");
                    MyImage* im=(MyImage*)duk_get_pointer(g_ctx,-1);
                    if (im) {
                        if (im->tex) g_R->destroy_image(im->tex);
                        free(im);
                    }
                    duk_pop(g_ctx);
                }
                duk_pop(g_ctx);
            }
        }
        duk_pop(g_ctx);
    }
    duk_pop_2(g_ctx);
    duk_destroy_heap(g_ctx);
    g_ctx=NULL;
}

static void jsi_setup_globals(int win_w, int win_h,
                               CanvasInfo* canvases, int canvas_count,
                               ImageInfo*  images,  int image_count) {
    g_win_w=win_w; g_win_h=win_h;
    if (canvas_count > MAX_IMAGES) canvas_count = MAX_IMAGES;
    if (image_count  > MAX_IMAGES) image_count  = MAX_IMAGES;
    memcpy(g_canvas_info, canvases, canvas_count * sizeof(CanvasInfo));
    g_canvas_count = canvas_count;
    memcpy(g_image_info,  images,   image_count  * sizeof(ImageInfo));
    g_image_count  = image_count;
    create_image(g_ctx);
    create_document(g_ctx);
    create_window_obj(g_ctx);
    create_screen_obj(g_ctx);
    create_navigator_obj(g_ctx);
    duk_push_c_function(g_ctx,js_alert,1);
    duk_put_global_string(g_ctx,"alert");
    create_audio_obj(g_ctx);
    create_localStorage(g_ctx);
    /* Expose window's timer/event fns at global scope too */
    duk_push_global_object(g_ctx);
    duk_get_prop_string(g_ctx,-1,"window");
    const char* copy_keys[]={
        "setTimeout","setInterval","clearTimeout","clearInterval",
        "addEventListener","requestAnimationFrame","cancelAnimationFrame",NULL
    };
    for (int i=0;copy_keys[i];i++) {
        duk_get_prop_string(g_ctx,-1,copy_keys[i]);
        duk_put_prop_string(g_ctx,-3,copy_keys[i]);
    }
    duk_pop_2(g_ctx);
}

static void jsi_preload_images(ImageInfo* images, int count) {
    for (int i=0;i<count;i++) {
        duk_push_global_object(g_ctx);
        duk_get_prop_string(g_ctx,-1,"document");
        duk_get_prop_string(g_ctx,-1,"images");
        duk_get_prop_index(g_ctx,-1,i);
        if (duk_is_object(g_ctx,-1)) {
            duk_push_string(g_ctx,"src");
            duk_push_string(g_ctx,images[i].src);
            duk_put_prop(g_ctx,-3);
        }
        duk_pop(g_ctx); duk_pop(g_ctx);
        duk_pop(g_ctx); duk_pop(g_ctx);
    }
}

static int jsi_eval_file(const char* path) {
    FILE* f=fopen(path,"rb");
    if (!f) {
        fprintf(stderr,"[eval_file] cannot open: %s\n",path);
        return 0;
    }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char* buf=(char*)malloc(sz+1);
    if (!buf) { fclose(f); return 0; }
    size_t rd=fread(buf,1,sz,f); fclose(f);
    if ((long)rd!=sz) { free(buf); return 0; }
    buf[sz]=0;
    duk_int_t rc=duk_peval_string(g_ctx,buf);
    free(buf);
    if (rc==DUK_EXEC_SUCCESS) { duk_pop(g_ctx); return 1; }
    fprintf(stderr,"[eval_file] %s: %s\n",path,duk_safe_to_string(g_ctx,-1));
    duk_pop(g_ctx); return 0;
}

static int jsi_eval_string(const char* code) {
    duk_int_t rc=duk_peval_string(g_ctx,code);
    if (rc==DUK_EXEC_SUCCESS) { duk_pop(g_ctx); return 1; }
    fprintf(stderr,"[eval_string] error: %s\n",duk_safe_to_string(g_ctx,-1));
    duk_pop(g_ctx); return 0;
}

static void jsi_call_window_onload(void) {
    if (g_window_onload_funcId<0) return;
    push_stored_func(g_ctx,g_window_onload_funcId);
    if (duk_is_callable(g_ctx,-1)) {
        duk_get_global_string(g_ctx,"window");
        if (duk_pcall_method(g_ctx,0)!=0)
            fprintf(stderr,"[window.onload] err: %s\n",duk_safe_to_string(g_ctx,-1));
        duk_pop(g_ctx);
    } else duk_pop(g_ctx);
}

static void jsi_call_window_load_listeners(void) {
    if (g_window_load_funcId<0) return;
    push_stored_func(g_ctx,g_window_load_funcId);
    if (duk_is_callable(g_ctx,-1)) {
        duk_get_global_string(g_ctx,"window");
        if (duk_pcall_method(g_ctx,0)!=0)
            fprintf(stderr,"[window load] err: %s\n",duk_safe_to_string(g_ctx,-1));
        duk_pop(g_ctx);
    } else duk_pop(g_ctx);
}

static void jsi_check_timers(void) { check_timers(g_ctx); }

static void jsi_dispatch_key(int keycode, int is_down) {
    if (is_down)
        dispatch_key_event(g_ctx,"keydown",keycode,
                           g_keydown_listeners,g_keydown_listener_count);
    else
        dispatch_key_event(g_ctx,"keyup",  keycode,
                           g_keyup_listeners, g_keyup_listener_count);
}

/* ============================================================================
 * Module entry point
 * ============================================================================ */
void jscore_duk_init_iface(JSCoreInterface* iface) {
    iface->init                      = jsi_init;
    iface->quit                      = jsi_quit;
    iface->setup_globals             = jsi_setup_globals;
    iface->preload_images            = jsi_preload_images;
    iface->eval_file                 = jsi_eval_file;
    iface->eval_string               = jsi_eval_string;
    iface->call_window_onload        = jsi_call_window_onload;
    iface->call_window_load_listeners= jsi_call_window_load_listeners;
    iface->check_timers              = jsi_check_timers;
    iface->dispatch_key              = jsi_dispatch_key;
}
