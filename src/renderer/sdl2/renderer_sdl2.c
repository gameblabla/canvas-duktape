#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <zlib.h>
#include "renderer_sdl2.h"

/* Extra debug logging - define EXTRA_DEBUG to enable verbose debug messages */
/* #define EXTRA_DEBUG */

/* ============================================================================
 * SDL2 State
 * ============================================================================ */
static SDL_Window*   g_window           = NULL;
static SDL_Renderer* g_sdl_renderer     = NULL;
static SDL_Texture*  g_offscreen        = NULL;
static int           g_win_w            = 120;
static int           g_win_h            = 160;

/* Resource directory for loading fonts and other assets */
static char g_resource_dir[1024] = {0};

/* Font cache */
#define MAX_FONTS 64
static TTF_Font* g_font_default = NULL;
typedef struct { char family[32]; int size; TTF_Font* font; } FontCacheEntry;
static FontCacheEntry g_font_cache[MAX_FONTS];
static int g_font_cache_count = 0;

/* Clip state */
static int g_has_clip = 0;
static int g_clip_x = 0, g_clip_y = 0, g_clip_w = 0, g_clip_h = 0;

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

/* Set resource directory for loading fonts and assets */
void renderer_sdl2_set_resource_dir(const char* path) {
    if (path) {
        strncpy(g_resource_dir, path, sizeof(g_resource_dir) - 1);
    } else {
        g_resource_dir[0] = '\0';
    }
}

/* Build full path for a resource file */
static void get_resource_path(const char* filename, char* out, size_t out_size) {
    if (g_resource_dir[0] != '\0') {
        snprintf(out, out_size, "%s/%s", g_resource_dir, filename);
    } else {
        strncpy(out, filename, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

static TTF_Font* get_font(const char* family, int size) {
    /* Check for bold/italic prefix (e.g., "bold Arial", "bold sans-serif") */
    int is_bold = 0, is_italic = 0;
    const char* fam = family ? family : "";
    if (strncmp(fam, "bold-italic ", 12) == 0) { is_bold = 1; is_italic = 1; fam += 12; }
    else if (strncmp(fam, "bold ", 5) == 0)    { is_bold = 1; fam += 5; }
    else if (strncmp(fam, "italic ", 7) == 0)  { is_italic = 1; fam += 7; }

    /* Normalize family name */
    char family_norm[32] = "sans-serif";
    if (fam && fam[0]) {
        if (strcmp(fam, "Arial") == 0 || strcmp(fam, "Helvetica") == 0 ||
            strcmp(fam, "Helvetica Neue") == 0 || strcmp(fam, "sans-serif") == 0) {
            strncpy(family_norm, "sans-serif", sizeof(family_norm) - 1);
        } else if (strcmp(fam, "Times") == 0 || strcmp(fam, "Times New Roman") == 0 ||
                   strcmp(fam, "Georgia") == 0 || strcmp(fam, "serif") == 0) {
            strncpy(family_norm, "serif", sizeof(family_norm) - 1);
        } else if (strcmp(fam, "Courier") == 0 || strcmp(fam, "Courier New") == 0 ||
                   strcmp(fam, "monospace") == 0 || strcmp(fam, "Lucida Console") == 0) {
            strncpy(family_norm, "monospace", sizeof(family_norm) - 1);
        } else {
            strncpy(family_norm, "sans-serif", sizeof(family_norm) - 1);
        }
    }

    /* Build cache key that includes bold/italic */
    char cache_key[48];
    snprintf(cache_key, sizeof(cache_key), "%s%s%s",
             is_bold ? "bold-" : "", is_italic ? "italic-" : "", family_norm);

    /* Map normalized family + style to TTF file */
    const char* ttf_file;
    if (strcmp(family_norm, "serif") == 0) {
        ttf_file = (is_bold && is_italic) ? "TTF/DejaVuSerif-BoldItalic.ttf" :
                   is_bold   ? "TTF/DejaVuSerif-Bold.ttf" :
                   is_italic ? "TTF/DejaVuSerif-Italic.ttf" :
                               "TTF/DejaVuSerif.ttf";
    } else if (strcmp(family_norm, "monospace") == 0) {
        ttf_file = (is_bold && is_italic) ? "TTF/DejaVuSansMono-BoldOblique.ttf" :
                   is_bold   ? "TTF/DejaVuSansMono-Bold.ttf" :
                   is_italic ? "TTF/DejaVuSansMono-Oblique.ttf" :
                               "TTF/DejaVuSansMono.ttf";
    } else {
        ttf_file = (is_bold && is_italic) ? "TTF/DejaVuSans-BoldOblique.ttf" :
                   is_bold   ? "TTF/DejaVuSans-Bold.ttf" :
                   is_italic ? "TTF/DejaVuSans-Oblique.ttf" :
                               "TTF/DejaVuSans.ttf";
    }

    /* Look up (cache_key, size) in cache */
    for (int i = 0; i < g_font_cache_count; i++) {
        if (g_font_cache[i].size == size &&
            strcmp(g_font_cache[i].family, cache_key) == 0) {
            return g_font_cache[i].font;
        }
    }

    /* Not found; open if cache not full */
    if (g_font_cache_count < MAX_FONTS) {
        char font_path[1024];
        get_resource_path(ttf_file, font_path, sizeof(font_path));
        TTF_Font* f = TTF_OpenFont(font_path, size);
        if (!f) {
            /* Fallback: try plain sans-serif */
            get_resource_path("TTF/DejaVuSans.ttf", font_path, sizeof(font_path));
            f = TTF_OpenFont(font_path, size);
        }
        if (f) {
            strncpy(g_font_cache[g_font_cache_count].family, cache_key,
                    sizeof(g_font_cache[g_font_cache_count].family) - 1);
            g_font_cache[g_font_cache_count].family[sizeof(g_font_cache[g_font_cache_count].family) - 1] = '\0';
            g_font_cache[g_font_cache_count].size = size;
            g_font_cache[g_font_cache_count].font = f;
            g_font_cache_count++;
            return f;
        }
    }
    return g_font_default;
}

static TTF_Font* get_font_for_size(int size) {
    return get_font("sans-serif", size);
}

/* Apply transform matrix to a destination rect (bounding box). */
static void apply_transform_to_dst(int dx, int dy, int dw, int dh,
                                    const double* m, SDL_Rect* out) {
    if (m[0]==1 && m[1]==0 && m[2]==0 && m[3]==1 && m[4]==0 && m[5]==0) {
        out->x = dx; out->y = dy; out->w = dw; out->h = dh;
        return;
    }
    double x1=dx,    y1=dy;
    double x2=dx+dw, y2=dy;
    double x3=dx,    y3=dy+dh;
    double x4=dx+dw, y4=dy+dh;
    double tx1=m[0]*x1+m[2]*y1+m[4], ty1=m[1]*x1+m[3]*y1+m[5];
    double tx2=m[0]*x2+m[2]*y2+m[4], ty2=m[1]*x2+m[3]*y2+m[5];
    double tx3=m[0]*x3+m[2]*y3+m[4], ty3=m[1]*x3+m[3]*y3+m[5];
    double tx4=m[0]*x4+m[2]*y4+m[4], ty4=m[1]*x4+m[3]*y4+m[5];
    double minX = fmin(fmin(tx1,tx2),fmin(tx3,tx4));
    double minY = fmin(fmin(ty1,ty2),fmin(ty3,ty4));
    double maxX = fmax(fmax(tx1,tx2),fmax(tx3,tx4));
    double maxY = fmax(fmax(ty1,ty2),fmax(ty3,ty4));
    out->x = (int)floor(minX);
    out->y = (int)floor(minY);
    out->w = (int)ceil(maxX - minX);
    out->h = (int)ceil(maxY - minY);
}

/* Render src_tex with full transform (scale/rotate/flip).
 * local_dw/local_dh are the destination size in LOCAL (pre-transform) coords. */
static void render_with_transform(SDL_Texture* src_tex,
                                   const SDL_Rect* s, const SDL_Rect* d,
                                   const double* m, Uint8 alphaMod,
                                   int orig_dx, int orig_dy,
                                   int local_dw, int local_dh) {
    if (m[0]==1 && m[1]==0 && m[2]==0 && m[3]==1 && m[4]==0 && m[5]==0) {
        SDL_SetTextureAlphaMod(src_tex, alphaMod);
        SDL_RenderCopy(g_sdl_renderer, src_tex, s, d);
        SDL_SetTextureAlphaMod(src_tex, 255);
        return;
    }
    int flip_h = (m[0] < 0) ? 1 : 0;
    int flip_v = (m[3] < 0) ? 1 : 0;
    int is_pure_flip = (fabs(m[1]) < 0.001 && fabs(m[2]) < 0.001
                     && fabs(m[4]) < 0.001 && fabs(m[5]) < 0.001);
    if (is_pure_flip && !flip_h && !flip_v) {
        SDL_SetTextureAlphaMod(src_tex, alphaMod);
        SDL_RenderCopy(g_sdl_renderer, src_tex, s, d);
        SDL_SetTextureAlphaMod(src_tex, 255);
        return;
    }
    if (is_pure_flip) {
        float scale_x = (float)d->w / (float)s->w;
        float scale_y = (float)d->h / (float)s->h;
        for (int sy = 0; sy < s->h; sy++) {
            int dy2 = flip_v ? (s->h - 1 - sy) : sy;
            int dst_y = d->y + (int)(dy2 * scale_y);
            int dst_h = (int)((dy2 + 1) * scale_y) - (int)(dy2 * scale_y);
            if (dst_h < 1) dst_h = 1;
            for (int sx2 = 0; sx2 < s->w; sx2++) {
                int dx2 = flip_h ? (s->w - 1 - sx2) : sx2;
                int dst_x = d->x + (int)(dx2 * scale_x);
                int dst_w2 = (int)((dx2+1)*scale_x) - (int)(dx2*scale_x);
                if (dst_w2 < 1) dst_w2 = 1;
                SDL_Rect sr = { s->x + sx2, s->y + sy, 1, 1 };
                SDL_Rect dr = { dst_x, dst_y, dst_w2, dst_h };
                SDL_SetTextureAlphaMod(src_tex, alphaMod);
                SDL_RenderCopy(g_sdl_renderer, src_tex, &sr, &dr);
            }
        }
        SDL_SetTextureAlphaMod(src_tex, 255);
        return;
    }
    /* Full rotation: map each source pixel through the full CTM.
     * Use local_dw/local_dh (destination size in LOCAL coords) as the
     * source→local scale, then apply the CTM to get world coords. */
    float scale_x = (s->w > 0) ? (float)local_dw / (float)s->w : 1.0f;
    float scale_y = (s->h > 0) ? (float)local_dh / (float)s->h : 1.0f;
    SDL_SetTextureAlphaMod(src_tex, alphaMod);
    for (int sy = s->y; sy < s->y + s->h; sy++) {
        for (int sx = s->x; sx < s->x + s->w; sx++) {
            float lx = orig_dx + (sx - s->x) * scale_x;
            float ly = orig_dy + (sy - s->y) * scale_y;
            double dx_d = m[0]*lx + m[2]*ly + m[4];
            double dy_d = m[1]*lx + m[3]*ly + m[5];
            int dx_i = (int)floor(dx_d);
            int dy_i = (int)floor(dy_d);
            SDL_Rect sr = { sx, sy, 1, 1 };
            SDL_Rect dr = { dx_i, dy_i, 1, 1 };
            SDL_RenderCopy(g_sdl_renderer, src_tex, &sr, &dr);
        }
    }
    SDL_SetTextureAlphaMod(src_tex, 255);
}

/* ============================================================================
 * Base64 / PNG helpers (used by to_data_url)
 * ============================================================================ */
static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char* base64_encode(const unsigned char* data, size_t len,
                            size_t* out_len) {
    *out_len = 4 * ((len + 2) / 3);
    char* enc = malloc(*out_len + 1);
    if (!enc) return NULL;
    for (size_t i = 0, j = 0; i < len; ) {
        uint32_t a = i < len ? data[i++] : 0;
        uint32_t b = i < len ? data[i++] : 0;
        uint32_t c = i < len ? data[i++] : 0;
        uint32_t t = (a << 16) | (b << 8) | c;
        enc[j++] = b64_table[(t >> 18) & 0x3f];
        enc[j++] = b64_table[(t >> 12) & 0x3f];
        enc[j++] = b64_table[(t >>  6) & 0x3f];
        enc[j++] = b64_table[ t        & 0x3f];
    }
    size_t mod = len % 3;
    if (mod) for (size_t i = 0; i < 3 - mod; i++) enc[*out_len - 1 - i] = '=';
    enc[*out_len] = '\0';
    return enc;
}

static unsigned char* deflate_data(const unsigned char* data, size_t len,
                                    size_t* out_len) {
    uLongf dest_len = compressBound(len);
    unsigned char* out = malloc(dest_len);
    if (!out || compress(out, &dest_len, data, len) != Z_OK) {
        free(out);
        *out_len = 0;
        return NULL;
    }
    *out_len = dest_len;
    return out;
}

static void png_write_chunk(unsigned char** out, size_t* out_len,
                             size_t* out_cap,
                             const char* type,
                             const unsigned char* data, size_t data_len) {
    size_t need = *out_len + 12 + data_len;
    if (need > *out_cap) {
        *out_cap = need * 2;
        *out = realloc(*out, *out_cap);
    }
    unsigned char* p = *out + *out_len;
    p[0] = (data_len >> 24) & 0xff;
    p[1] = (data_len >> 16) & 0xff;
    p[2] = (data_len >>  8) & 0xff;
    p[3] =  data_len        & 0xff;
    memcpy(p + 4, type, 4);
    if (data_len > 0) memcpy(p + 8, data, data_len);
    unsigned char* crc_buf = malloc(4 + data_len);
    memcpy(crc_buf, type, 4);
    if (data_len > 0) memcpy(crc_buf + 4, data, data_len);
    unsigned long c = crc32(0L, crc_buf, (uInt)(4 + data_len));
    free(crc_buf);
    p[8 + data_len + 0] = (c >> 24) & 0xff;
    p[8 + data_len + 1] = (c >> 16) & 0xff;
    p[8 + data_len + 2] = (c >>  8) & 0xff;
    p[8 + data_len + 3] =  c        & 0xff;
    *out_len += 12 + data_len;
}

/* Canvas textures store premultiplied alpha (drawing via SDL_BLENDMODE_BLEND
 * premultiplies src_RGB by src_A). Compositing FROM them requires ONE +
 * ONE_MINUS_SRC_ALPHA so alpha isn't applied twice. */
static SDL_BlendMode get_premult_blend_mode(void) {
    return SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD);
}

/* ============================================================================
 * Interface implementations
 * ============================================================================ */
static int r_init(int w, int h, const char* title) {
    g_win_w = w; g_win_h = h;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 0;
    }
    if (!(IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) & (IMG_INIT_PNG | IMG_INIT_JPG))) {
        fprintf(stderr, "IMG_Init: %s\n", IMG_GetError());
        SDL_Quit(); return 0;
    }
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        IMG_Quit(); SDL_Quit(); return 0;
    }
    char font_path[1024];
    get_resource_path("TTF/DejaVuSans.ttf", font_path, sizeof(font_path));
    g_font_default = TTF_OpenFont(font_path, 20);
    if (!g_font_default) {
        fprintf(stderr, "Warning: failed to load TTF/DejaVuSans.ttf: %s\n", TTF_GetError());
    }

    g_window = SDL_CreateWindow(title,
                                SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                w, h, 0);
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        TTF_Quit(); IMG_Quit(); SDL_Quit(); return 0;
    }
    g_sdl_renderer = SDL_CreateRenderer(g_window, -1,
                                        SDL_RENDERER_ACCELERATED |
                                        SDL_RENDERER_PRESENTVSYNC);
    if (!g_sdl_renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        TTF_Quit(); IMG_Quit(); SDL_Quit(); return 0;
    }
    SDL_RenderSetLogicalSize(g_sdl_renderer, w, h);

    g_offscreen = SDL_CreateTexture(g_sdl_renderer,
                                    SDL_PIXELFORMAT_RGBA8888,
                                    SDL_TEXTUREACCESS_TARGET, w, h);
    if (!g_offscreen) {
        fprintf(stderr, "create offscreen: %s\n", SDL_GetError());
        SDL_DestroyRenderer(g_sdl_renderer);
        SDL_DestroyWindow(g_window);
        TTF_Quit(); IMG_Quit(); SDL_Quit(); return 0;
    }
    SDL_SetTextureBlendMode(g_offscreen, get_premult_blend_mode());
    SDL_SetRenderTarget(g_sdl_renderer, g_offscreen);
    SDL_SetRenderDrawColor(g_sdl_renderer, 0, 0, 0, 0);
    SDL_RenderClear(g_sdl_renderer);
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
    return 1;
}

static void r_quit(void) {
    for (int i = 0; i < g_font_cache_count; i++)
        TTF_CloseFont(g_font_cache[i].font);
    g_font_cache_count = 0;
    if (g_font_default) { TTF_CloseFont(g_font_default); g_font_default = NULL; }
    if (g_offscreen)    { SDL_DestroyTexture(g_offscreen); g_offscreen = NULL; }
    if (g_sdl_renderer) { SDL_DestroyRenderer(g_sdl_renderer); g_sdl_renderer = NULL; }
    if (g_window)       { SDL_DestroyWindow(g_window); g_window = NULL; }
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

static void* r_create_texture(int w, int h) {
    SDL_Texture* t = SDL_CreateTexture(g_sdl_renderer,
                                       SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_TARGET, w, h);
#ifdef EXTRA_DEBUG
    fprintf(stderr, "[r_create_texture] %dx%d -> %p\n", w, h, t);
#endif
    if (!t) return NULL;
    SDL_SetTextureBlendMode(t, get_premult_blend_mode());
    SDL_SetRenderTarget(g_sdl_renderer, t);
    SDL_SetRenderDrawColor(g_sdl_renderer, 0, 0, 0, 0);
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_RenderClear(g_sdl_renderer);
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
    return t;
}

static void r_destroy_texture(void* tex) {
    if (tex) SDL_DestroyTexture((SDL_Texture*)tex);
}

static void* r_get_main_texture(void) { return g_offscreen; }

static void* r_load_image_file(const char* path) {
    /* Resolve resource path */
    char full_path[1024];
    get_resource_path(path, full_path, sizeof(full_path));
    
    SDL_Surface* sf = IMG_Load(full_path);
    if (!sf) {
        fprintf(stderr, "[load_image_file] %s: %s\n", full_path, IMG_GetError());
        return NULL;
    }
    SDL_Texture* t = SDL_CreateTextureFromSurface(g_sdl_renderer, sf);
    if (!t)
        fprintf(stderr, "[load_image_file] texture from %s: %s\n", full_path, SDL_GetError());
    SDL_FreeSurface(sf);
    return t;
}

static void* r_load_image_mem(const unsigned char* data, int len) {
    SDL_RWops* rw = SDL_RWFromConstMem(data, len);
    if (!rw) return NULL;
    SDL_Surface* sf = IMG_LoadPNG_RW(rw);
    SDL_FreeRW(rw);
    if (!sf) return NULL;
    SDL_Texture* t = SDL_CreateTextureFromSurface(g_sdl_renderer, sf);
    SDL_FreeSurface(sf);
    return t;
}

static void r_destroy_image(void* img) {
    if (img) SDL_DestroyTexture((SDL_Texture*)img);
}

static void r_get_image_size(void* img, int* w, int* h) {
    if (!img) { *w = 0; *h = 0; return; }
    SDL_QueryTexture((SDL_Texture*)img, NULL, NULL, w, h);
}

/* Apply stored clip rect to current render target, if any. */
static void apply_clip(void) {
    if (g_has_clip) {
        SDL_Rect cr = { g_clip_x, g_clip_y, g_clip_w, g_clip_h };
        SDL_RenderSetClipRect(g_sdl_renderer, &cr);
    }
}
static void remove_clip(void) {
    if (g_has_clip) SDL_RenderSetClipRect(g_sdl_renderer, NULL);
}

static void r_fill_rect(void* target, int x, int y, int w, int h,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                         int blend_add, const double* m) {
    SDL_Texture* tex = (SDL_Texture*)target;
    if (!tex) return;
    if (SDL_SetRenderTarget(g_sdl_renderer, tex) != 0) return;
    apply_clip();
    SDL_BlendMode bm;
    if (blend_add == 1) {
        bm = SDL_BLENDMODE_ADD;
    } else if (blend_add == 2) {
        /* destination-over: new pixels go behind existing opaque pixels */
        bm = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 3) {
        /* copy: clear entire canvas, then draw with BLENDMODE_NONE */
        SDL_SetRenderDrawColor(g_sdl_renderer, 0, 0, 0, 0);
        SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
        SDL_RenderFillRect(g_sdl_renderer, NULL); /* clear entire target */
        bm = SDL_BLENDMODE_NONE;
    } else if (blend_add == 4) { /* source-in: Src * DstA */
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 5) { /* source-out: Src * (1-DstA) */
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 6) { /* destination-in: Dst * SrcA, clear outside */
        int tex_w = 0, tex_h = 0;
        SDL_QueryTexture(tex, NULL, NULL, &tex_w, &tex_h);
        int rx1 = x < 0 ? 0 : x;
        int ry1 = y < 0 ? 0 : y;
        int rx2 = (x+w) > tex_w ? tex_w : (x+w);
        int ry2 = (y+h) > tex_h ? tex_h : (y+h);
        SDL_SetRenderDrawColor(g_sdl_renderer, 0, 0, 0, 0);
        SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
        if (ry1 > 0) { SDL_Rect t = {0, 0, tex_w, ry1}; SDL_RenderFillRect(g_sdl_renderer, &t); }
        if (rx1 > 0) { SDL_Rect t = {0, ry1, rx1, ry2-ry1}; SDL_RenderFillRect(g_sdl_renderer, &t); }
        if (rx2 < tex_w) { SDL_Rect t = {rx2, ry1, tex_w-rx2, ry2-ry1}; SDL_RenderFillRect(g_sdl_renderer, &t); }
        if (ry2 < tex_h) { SDL_Rect t = {0, ry2, tex_w, tex_h-ry2}; SDL_RenderFillRect(g_sdl_renderer, &t); }
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 7) { /* xor: Src*(1-DstA) + Dst*(1-SrcA) */
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 8) { /* multiply */
        bm = SDL_BLENDMODE_MOD;
    } else if (blend_add == 9) { /* source-atop: Src*DstA + Dst*(1-SrcA) */
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 10) { /* destination-out: Dst*(1-SrcA) */
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 11) { /* destination-atop: Src*(1-DstA) + Dst*SrcA */
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    } else {
        bm = (a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    }
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, bm);
    SDL_SetRenderDrawColor(g_sdl_renderer, r, g, b, a);

    int is_identity = (m[0]==1 && m[1]==0 && m[2]==0 &&
                       m[3]==1 && m[4]==0 && m[5]==0);
    if (!is_identity) {
        /* Scanline-fill the transformed quad */
        double x1=x,   y1=y;
        double x2=x+w, y2=y;
        double x3=x+w, y3=y+h;
        double x4=x,   y4=y+h;
        double tx1=m[0]*x1+m[2]*y1+m[4], ty1=m[1]*x1+m[3]*y1+m[5];
        double tx2=m[0]*x2+m[2]*y2+m[4], ty2=m[1]*x2+m[3]*y2+m[5];
        double tx3=m[0]*x3+m[2]*y3+m[4], ty3=m[1]*x3+m[3]*y3+m[5];
        double tx4=m[0]*x4+m[2]*y4+m[4], ty4=m[1]*x4+m[3]*y4+m[5];
        int min_y = (int)floor(fmin(fmin(ty1,ty2),fmin(ty3,ty4)));
        int max_y = (int)ceil (fmax(fmax(ty1,ty2),fmax(ty3,ty4)));
        double px[4]={tx1,tx2,tx3,tx4}, py[4]={ty1,ty2,ty3,ty4};
        for (int scan_y = min_y; scan_y <= max_y; scan_y++) {
            double ixs[8]; int cnt = 0;
            for (int e = 0; e < 4; e++) {
                int next = (e+1)%4;
                double ey0=py[e], ey1=py[next];
                double ex0=px[e], ex1=px[next];
                if (ey0==ey1) continue;
                double emi=fmin(ey0,ey1), ema=fmax(ey0,ey1);
                if ((double)scan_y > emi && (double)scan_y <= ema) {
                    double t = ((double)scan_y - ey0) / (ey1 - ey0);
                    ixs[cnt++] = ex0 + t*(ex1-ex0);
                }
            }
            for (int i=0;i<cnt-1;i++) for(int j=i+1;j<cnt;j++)
                if(ixs[i]>ixs[j]){double tmp=ixs[i];ixs[i]=ixs[j];ixs[j]=tmp;}
            for (int i=0;i<cnt-1;i+=2)
                SDL_RenderDrawLine(g_sdl_renderer,
                    (int)floor(ixs[i]), scan_y, (int)ceil(ixs[i+1]), scan_y);
        }
    } else {
        SDL_RenderFillRect(g_sdl_renderer, &((SDL_Rect){x,y,w,h}));
    }
    remove_clip();
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
    SDL_RenderFlush(g_sdl_renderer);
}

static void r_fill_rect_pattern(void* target, int x, int y, int w, int h,
                                 void* img) {
    SDL_Texture* tex = (SDL_Texture*)target;
    SDL_Texture* pat = (SDL_Texture*)img;
    if (!tex || !pat) return;
    int pw, ph;
    SDL_QueryTexture(pat, NULL, NULL, &pw, &ph);
    if (SDL_SetRenderTarget(g_sdl_renderer, tex) != 0) return;
    /* Clear any SDL clip rect on this target */
    SDL_RenderSetClipRect(g_sdl_renderer, NULL);
    /* Tile the pattern */
    for (int ty = y; ty < y+h; ty += ph) {
        for (int tx = x; tx < x+w; tx += pw) {
            int dw = (tx+pw > x+w) ? (x+w-tx) : pw;
            int dh = (ty+ph > y+h) ? (y+h-ty) : ph;
            SDL_Rect src = {0, 0, dw, dh};
            SDL_Rect dst = {tx, ty, dw, dh};
            SDL_RenderCopy(g_sdl_renderer, pat, &src, &dst);
        }
    }
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
}

static void r_clear_rect(void* target, int x, int y, int w, int h) {
    SDL_Texture* tex = (SDL_Texture*)target;
    if (!tex) return;
    if (SDL_SetRenderTarget(g_sdl_renderer, tex) != 0) return;
    SDL_SetRenderDrawColor(g_sdl_renderer, 0, 0, 0, 0);
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_RenderFillRect(g_sdl_renderer, &((SDL_Rect){x,y,w,h}));
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
}

static void r_stroke_rect(void* target, double x, double y, double w, double h,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a, int lw,
                           int blend_add) {
    SDL_Texture* tex = (SDL_Texture*)target;
    if (!tex) return;
    if (SDL_SetRenderTarget(g_sdl_renderer, tex) != 0) return;
    SDL_SetRenderDrawColor(g_sdl_renderer, r, g, b, a);
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, blend_add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    int half_lw = lw / 2;
    for (int i = 0; i < lw; i++) {
        int off = i - half_lw;
        SDL_RenderDrawRect(g_sdl_renderer,
            &((SDL_Rect){(int)x+off,(int)y+off,(int)w-2*off,(int)h-2*off}));
    }
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
}

static void r_draw_image(void* target, void* img,
                          int sx, int sy, int sw, int sh,
                          int dx, int dy, int dw, int dh,
                          const double* m, uint8_t alpha) {
    SDL_Texture* dst = (SDL_Texture*)target;
    SDL_Texture* src = (SDL_Texture*)img;
    if (!dst || !src) return;
    SDL_Rect srcRect = {sx, sy, sw, sh};
    SDL_Rect dstRect;
    apply_transform_to_dst(dx, dy, dw, dh, m, &dstRect);
    SDL_SetTextureAlphaMod(src, alpha);
    SDL_SetRenderTarget(g_sdl_renderer, dst);
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_BLEND);
    render_with_transform(src, &srcRect, &dstRect, m, alpha, dx, dy, dw, dh);
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
    SDL_SetTextureAlphaMod(src, 255);
}

static void r_draw_canvas(void* target, void* src_tex,
                           int sx, int sy, int sw, int sh,
                           int dx, int dy, int dw, int dh,
                           const double* m, uint8_t alpha) {
    r_draw_image(target, src_tex, sx, sy, sw, sh, dx, dy, dw, dh, m, alpha);
}

static void r_fill_text(void* target, const char* text, double x, double y,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                         int font_size, const char* align,
                         const char* baseline, const char* font_family) {
    SDL_Texture* tex = (SDL_Texture*)target;
    if (!tex || !text || !text[0]) return;
    TTF_Font* font = get_font(font_family, font_size);
    if (!font) return;
    SDL_Color fg = {r, g, b, 255};
    SDL_Surface* sf = TTF_RenderUTF8_Blended(font, text, fg);
    if (!sf) return;
    SDL_Texture* tt = SDL_CreateTextureFromSurface(g_sdl_renderer, sf);
    SDL_FreeSurface(sf);
    if (!tt) return;
    int tw, th;
    SDL_QueryTexture(tt, NULL, NULL, &tw, &th);
    int rx = (int)x;
    if (align && strcmp(align, "center") == 0) rx -= tw / 2;
    else if (align && (strcmp(align, "right")==0 || strcmp(align,"end")==0)) rx -= tw;
    /* SDL_TTF: ascent is positive, descent is negative.
     * Surface top pixel = baseline - ascent.
     * Baseline calculations to match HTML5 Canvas spec: */
    int ascent  = TTF_FontAscent(font);
    int descent = TTF_FontDescent(font); /* negative in SDL_TTF */
    int ry;
    if (baseline && (strcmp(baseline, "top") == 0 || strcmp(baseline, "hanging") == 0)) {
        ry = (int)y;
    } else if (baseline && strcmp(baseline, "middle") == 0) {
        ry = (int)y - (ascent - descent) / 2; /* descent negative → ascent - descent = total height */
    } else if (baseline && (strcmp(baseline, "bottom") == 0 || strcmp(baseline, "ideographic") == 0)) {
        ry = (int)y + descent - ascent; /* descent negative: y - |descent| - ascent */
    } else {
        /* "alphabetic" (default) and anything else */
        ry = (int)y - ascent;
    }
    SDL_SetRenderTarget(g_sdl_renderer, tex);
    SDL_Rect dst = {rx, ry, tw, th};
    SDL_RenderCopy(g_sdl_renderer, tt, NULL, &dst);
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
    SDL_DestroyTexture(tt);
}

static void r_stroke_text(void* target, const char* text, double x, double y,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                           int font_size, int lw, const char* font_family) {
    SDL_Texture* tex = (SDL_Texture*)target;
    if (!tex || !text || !text[0]) return;
    TTF_Font* font = get_font(font_family, font_size);
    if (!font) return;
    SDL_Color fg = {r, g, b, 255};
    SDL_Surface* sf = TTF_RenderUTF8_Blended(font, text, fg);
    if (!sf) return;
    SDL_Texture* tt = SDL_CreateTextureFromSurface(g_sdl_renderer, sf);
    SDL_FreeSurface(sf);
    if (!tt) return;
    int tw, th;
    SDL_QueryTexture(tt, NULL, NULL, &tw, &th);
    int ascent = TTF_FontAscent(font);
    int ry = (int)y - ascent; /* stroke_text uses alphabetic baseline */
    if (lw < 1) lw = 1;
    SDL_SetRenderTarget(g_sdl_renderer, tex);
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_BLEND);
    for (int ox = -lw; ox <= lw; ox++) {
        for (int oy = -lw; oy <= lw; oy++) {
            if (ox*ox + oy*oy > lw*lw) continue;
            if (ox*ox + oy*oy < (lw-1)*(lw-1)) continue;
            SDL_Rect dst = {(int)x + ox, ry + oy, tw, th};
            SDL_RenderCopy(g_sdl_renderer, tt, NULL, &dst);
        }
    }
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
    SDL_DestroyTexture(tt);
}

static int is_monospace_family(const char* family) {
    if (!family) return 0;
    return (strcmp(family, "Courier") == 0 || strcmp(family, "Courier New") == 0 ||
            strcmp(family, "monospace") == 0 || strcmp(family, "Lucida Console") == 0);
}

/* Count UTF-8 code points (non-continuation bytes) */
static int utf8_char_count(const char* s) {
    int n = 0;
    while (*s) { if ((*s & 0xC0) != 0x80) n++; s++; }
    return n;
}

static int r_measure_text(const char* text, int font_size, const char* font_family) {
    if (!text) return 0;
    TTF_Font* font = get_font(font_family, font_size);
    if (!font) return (int)(strlen(text) * 8);
    if (is_monospace_family(font_family)) {
        int cw = 0, ch = 0;
        TTF_SizeUTF8(font, "M", &cw, &ch);
        return cw * utf8_char_count(text);
    }
    int w = 0, h = 0;
    TTF_SizeUTF8(font, text, &w, &h);
    return w;
}

static void r_measure_text_ex(const char* text, int font_size, const char* font_family,
                               int* out_width, int* out_ascent, int* out_descent) {
    if (!text || !out_width || !out_ascent || !out_descent) return;
    *out_width = 0; *out_ascent = 0; *out_descent = 0;
    TTF_Font* font = get_font(font_family, font_size);
    if (!font) { *out_width = (int)(strlen(text) * 8); return; }
    if (is_monospace_family(font_family)) {
        int cw = 0, ch = 0;
        TTF_SizeUTF8(font, "M", &cw, &ch);
        *out_width = cw * utf8_char_count(text);
    } else {
        int w = 0, h = 0;
        TTF_SizeUTF8(font, text, &w, &h);
        *out_width = w;
    }
    *out_ascent = TTF_FontAscent(font);
    *out_descent = -TTF_FontDescent(font);  /* TTF returns negative, we want positive */
}

static void r_draw_arc_points(void* target,
                               double cx, double cy, double radius,
                               double start_angle, double end_angle, int ccw,
                               uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_Texture* tex = (SDL_Texture*)target;
    if (!tex) return;
    SDL_SetRenderTarget(g_sdl_renderer, tex);
    SDL_SetRenderDrawColor(g_sdl_renderer, r, g, b, a);
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_BLEND);
    double step = 1.0 / radius;
    if (step > 0.1) step = 0.1;
    double angle_step = ccw ? -step : step;
    for (double angle = start_angle;
         ccw ? (angle >= end_angle) : (angle <= end_angle);
         angle += angle_step) {
        SDL_RenderDrawPoint(g_sdl_renderer,
            (int)(cx + radius * cos(angle)),
            (int)(cy + radius * sin(angle)));
    }
    SDL_RenderDrawPoint(g_sdl_renderer,
        (int)(cx + radius * cos(end_angle)),
        (int)(cy + radius * sin(end_angle)));
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
}

static void r_fill_polygon(void* target, const double* pts, int count,
                            uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                            int blend_add, int fill_rule) {
    SDL_Texture* tex = (SDL_Texture*)target;
    if (!tex || count < 3) return;
    SDL_SetRenderTarget(g_sdl_renderer, tex);
    SDL_BlendMode bm;
    if (blend_add == 1) {
        bm = SDL_BLENDMODE_ADD;
    } else if (blend_add == 2) {
        bm = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 3) {
        SDL_SetRenderDrawColor(g_sdl_renderer, 0, 0, 0, 0);
        SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
        SDL_RenderFillRect(g_sdl_renderer, NULL);
        bm = SDL_BLENDMODE_NONE;
    } else if (blend_add == 4) {
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 5) {
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 6) {
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 7) {
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 8) {
        bm = SDL_BLENDMODE_MOD;
    } else if (blend_add == 9) { /* source-atop: Src*DstA + Dst*(1-SrcA) */
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 10) { /* destination-out: Dst*(1-SrcA) */
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    } else if (blend_add == 11) { /* destination-atop: Src*(1-DstA) + Dst*SrcA */
        bm = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                        SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    } else {
        bm = (a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    }
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, bm);
    SDL_SetRenderDrawColor(g_sdl_renderer, r, g, b, a);
    double min_x=pts[0], max_x=pts[0], min_y=pts[1], max_y=pts[1];
    for (int i=1; i<count; i++) {
        double px=pts[i*2], py=pts[i*2+1];
        if(px<min_x)min_x=px; if(px>max_x)max_x=px;
        if(py<min_y)min_y=py; if(py>max_y)max_y=py;
    }
    for (int scan_y = (int)floor(min_y); scan_y <= (int)ceil(max_y); scan_y++) {
        struct { double x; int w; } ixs[128]; int cnt = 0;
        for (int i=0; i<count-1; i++) {
            double x1=pts[i*2], y1=pts[i*2+1];
            double x2=pts[(i+1)*2], y2=pts[(i+1)*2+1];
            if((y1<=scan_y && y2>scan_y)||(y2<=scan_y && y1>scan_y)) {
                if (cnt < 127) {
                    ixs[cnt].x = x1 + (scan_y-y1)/(y2-y1)*(x2-x1);
                    ixs[cnt].w = (y2 > y1) ? 1 : -1;
                    cnt++;
                }
            }
        }
        for(int i=0;i<cnt-1;i++) for(int j=i+1;j<cnt;j++)
            if(ixs[i].x>ixs[j].x){
                double tx=ixs[i].x; int tw=ixs[i].w;
                ixs[i].x=ixs[j].x; ixs[i].w=ixs[j].w;
                ixs[j].x=tx; ixs[j].w=tw;
            }
        if (fill_rule == 1) { /* nonzero winding */
            int winding = 0;
            int span_start = 0;
            for (int i = 0; i < cnt; i++) {
                int prev = winding;
                winding += ixs[i].w;
                if (prev == 0 && winding != 0)
                    span_start = (int)floor(ixs[i].x);
                else if (prev != 0 && winding == 0)
                    SDL_RenderDrawLine(g_sdl_renderer, span_start, scan_y,
                                       (int)ceil(ixs[i].x), scan_y);
            }
        } else { /* evenodd */
            for(int i=0;i<cnt-1;i+=2)
                SDL_RenderDrawLine(g_sdl_renderer,
                    (int)floor(ixs[i].x), scan_y, (int)ceil(ixs[i+1].x), scan_y);
        }
    }
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
}

static void r_fill_circle(void* target, double cx, double cy, int radius,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                           int blend_add) {
    SDL_Texture* tex = (SDL_Texture*)target;
    if (!tex) return;
    SDL_SetRenderTarget(g_sdl_renderer, tex);
    SDL_SetRenderDrawColor(g_sdl_renderer, r, g, b, a);
    SDL_SetRenderDrawBlendMode(g_sdl_renderer,
        blend_add ? SDL_BLENDMODE_ADD :
        (a < 255  ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE));
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)sqrt((double)(radius*radius - dy*dy));
        SDL_RenderDrawLine(g_sdl_renderer,
            (int)cx - dx, (int)cy + dy,
            (int)cx + dx, (int)cy + dy);
    }
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
}

static void r_stroke_circle(void* target, double cx, double cy, int radius,
                             uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_Texture* tex = (SDL_Texture*)target;
    if (!tex) return;
    SDL_SetRenderTarget(g_sdl_renderer, tex);
    SDL_SetRenderDrawColor(g_sdl_renderer, r, g, b, a);
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_BLEND);
    for (double angle = 0; angle < 2*M_PI; angle += (radius > 0 ? 1.0/radius : 0.1)) {
        SDL_RenderDrawPoint(g_sdl_renderer,
            (int)(cx + radius * cos(angle)),
            (int)(cy + radius * sin(angle)));
    }
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
}

static void r_draw_line(void* target, int x1, int y1, int x2, int y2,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_Texture* tex = (SDL_Texture*)target;
    if (!tex) return;
    SDL_SetRenderTarget(g_sdl_renderer, tex);
    SDL_SetRenderDrawColor(g_sdl_renderer, r, g, b, a);
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderDrawLine(g_sdl_renderer, x1, y1, x2, y2);
    SDL_SetRenderDrawBlendMode(g_sdl_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
}

static void r_set_clip_rect(void* target, int x, int y, int w, int h) {
    g_has_clip = 1;
    g_clip_x = x; g_clip_y = y; g_clip_w = w; g_clip_h = h;
    SDL_Texture* tex = (SDL_Texture*)target;
    if (tex) {
        SDL_SetRenderTarget(g_sdl_renderer, tex);
        SDL_Rect cr = {x, y, w, h};
        SDL_RenderSetClipRect(g_sdl_renderer, &cr);
        SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
    }
}

static void r_clear_clip_rect(void* target) {
    g_has_clip = 0;
    SDL_Texture* tex = (SDL_Texture*)target;
    if (tex) {
        SDL_SetRenderTarget(g_sdl_renderer, tex);
        SDL_RenderSetClipRect(g_sdl_renderer, NULL);
        SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
    }
}

static void r_get_pixels(void* target, int x, int y, int w, int h,
                          uint8_t* rgba_out) {
    SDL_Texture* tex = (SDL_Texture*)target;
    if (!tex || !rgba_out) return;
    SDL_SetRenderTarget(g_sdl_renderer, tex);
    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                                     SDL_PIXELFORMAT_ABGR8888);
    if (sf) {
        if (SDL_RenderReadPixels(g_sdl_renderer,
                                 &((SDL_Rect){x,y,w,h}),
                                 sf->format->format,
                                 sf->pixels, sf->pitch) == 0) {
            Uint32* px = (Uint32*)sf->pixels;
            for (int i = 0; i < w*h; i++) {
                Uint32 p = px[i];
                rgba_out[i*4+0] =  p        & 0xff; /* R */
                rgba_out[i*4+1] = (p >>  8) & 0xff; /* G */
                rgba_out[i*4+2] = (p >> 16) & 0xff; /* B */
                rgba_out[i*4+3] = (p >> 24) & 0xff; /* A */
            }
        }
        SDL_FreeSurface(sf);
    }
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
}

static void r_put_pixels(void* target, const uint8_t* rgba,
                          int x, int y, int w, int h) {
    SDL_Texture* tex = (SDL_Texture*)target;
    if (!tex || !rgba) return;
    Uint32* buf = malloc(w * h * 4);
    if (!buf) return;
    for (int i = 0; i < w*h; i++) {
        buf[i] = ((Uint32)rgba[i*4+0] << 24)
               | ((Uint32)rgba[i*4+1] << 16)
               | ((Uint32)rgba[i*4+2] <<  8)
               |  (Uint32)rgba[i*4+3];
    }
    SDL_Texture* tmp = SDL_CreateTexture(g_sdl_renderer,
                                         SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_STREAMING, w, h);
    if (tmp) {
        SDL_UpdateTexture(tmp, NULL, buf, w * 4);
        SDL_SetTextureBlendMode(tmp, SDL_BLENDMODE_NONE);
        SDL_SetRenderTarget(g_sdl_renderer, tex);
        /* Apply logical clip rect (don't let stale SDL per-target clip interfere) */
        if (g_has_clip) {
            SDL_Rect cr = {g_clip_x, g_clip_y, g_clip_w, g_clip_h};
            SDL_RenderSetClipRect(g_sdl_renderer, &cr);
        } else {
            SDL_RenderSetClipRect(g_sdl_renderer, NULL);
        }
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(g_sdl_renderer, tmp, NULL, &dst);
        SDL_SetRenderTarget(g_sdl_renderer, NULL);
        SDL_RenderFlush(g_sdl_renderer);
        SDL_DestroyTexture(tmp);
    }
    free(buf);
}

static char* r_to_data_url(void* target, int w, int h) {
    SDL_Texture* tex = (SDL_Texture*)target;
    if (!tex || !g_sdl_renderer || w <= 0 || h <= 0)
        return NULL;
    SDL_SetRenderTarget(g_sdl_renderer, tex);
    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                                     SDL_PIXELFORMAT_RGBA8888);
    if (!sf) { SDL_SetRenderTarget(g_sdl_renderer, NULL); return NULL; }
    if (SDL_RenderReadPixels(g_sdl_renderer, NULL,
                             SDL_PIXELFORMAT_RGBA8888,
                             sf->pixels, sf->pitch) != 0) {
        SDL_FreeSurface(sf);
        SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);
        return NULL;
    }
    SDL_SetRenderTarget(g_sdl_renderer, NULL);
    SDL_RenderFlush(g_sdl_renderer);

    /* Build raw PNG data */
    size_t png_cap = 100 + (size_t)w * h * 4;
    size_t png_len = 0;
    unsigned char* png_data = malloc(png_cap);
    unsigned char sig[8] = {137,80,78,71,13,10,26,10};
    memcpy(png_data, sig, 8); png_len = 8;

    unsigned char ihdr[13];
    ihdr[0]=(w>>24)&0xff; ihdr[1]=(w>>16)&0xff;
    ihdr[2]=(w>> 8)&0xff; ihdr[3]= w     &0xff;
    ihdr[4]=(h>>24)&0xff; ihdr[5]=(h>>16)&0xff;
    ihdr[6]=(h>> 8)&0xff; ihdr[7]= h     &0xff;
    ihdr[8]=8; ihdr[9]=6; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0;
    png_write_chunk(&png_data, &png_len, &png_cap, "IHDR", ihdr, 13);

    size_t raw_len = (size_t)h * (1 + w * 4);
    unsigned char* raw = malloc(raw_len);
    for (int row = 0; row < h; row++) {
        unsigned char* out_row = raw + row * (1 + w * 4);
        out_row[0] = 0; /* filter: none */
        Uint32* px = (Uint32*)((unsigned char*)sf->pixels + row * sf->pitch);
        for (int col = 0; col < w; col++) {
            Uint32 p = px[col];
            /* SDL RGBA8888: R=bits[31:24] in big-endian reading */
            out_row[1 + col*4 + 0] = (p >>  0) & 0xff; /* stored as R */
            out_row[1 + col*4 + 1] = (p >>  8) & 0xff;
            out_row[1 + col*4 + 2] = (p >> 16) & 0xff;
            out_row[1 + col*4 + 3] = (p >> 24) & 0xff;
        }
    }
    SDL_FreeSurface(sf);

    size_t clen;
    unsigned char* comp = deflate_data(raw, raw_len, &clen);
    free(raw);
    png_write_chunk(&png_data, &png_len, &png_cap, "IDAT", comp, clen);
    free(comp);
    png_write_chunk(&png_data, &png_len, &png_cap, "IEND", NULL, 0);

    size_t b64len;
    char* b64 = base64_encode(png_data, png_len, &b64len);
    free(png_data);
    if (!b64) return NULL;

    char* url = malloc(22 + b64len + 1);
    sprintf(url, "data:image/png;base64,%s", b64);
    free(b64);
    return url;
}

static void r_present(void) {
    SDL_SetRenderDrawColor(g_sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_sdl_renderer);
    if (g_offscreen)
        SDL_RenderCopy(g_sdl_renderer, g_offscreen, NULL, NULL);
    SDL_RenderPresent(g_sdl_renderer);
}

static void r_clear_main(void) {
    /* no-op — offscreen is cleared per-frame by JS code */
}

static double r_get_time_ms(void) { return (double)SDL_GetTicks(); }
static void   r_sleep_ms(int ms)  { SDL_Delay(ms); }

/* ============================================================================
 * Interface initializer
 * ============================================================================ */
void renderer_sdl2_init_iface(RendererInterface* iface) {
    iface->init             = r_init;
    iface->quit             = r_quit;
    iface->create_texture   = r_create_texture;
    iface->destroy_texture  = r_destroy_texture;
    iface->get_main_texture = r_get_main_texture;
    iface->load_image_file  = r_load_image_file;
    iface->load_image_mem   = r_load_image_mem;
    iface->destroy_image    = r_destroy_image;
    iface->get_image_size   = r_get_image_size;
    iface->fill_rect        = r_fill_rect;
    iface->fill_rect_pattern= r_fill_rect_pattern;
    iface->clear_rect       = r_clear_rect;
    iface->stroke_rect      = r_stroke_rect;
    iface->draw_image       = r_draw_image;
    iface->draw_canvas      = r_draw_canvas;
    iface->fill_text        = r_fill_text;
    iface->stroke_text      = r_stroke_text;
    iface->measure_text     = r_measure_text;
    iface->measure_text_ex  = r_measure_text_ex;
    iface->draw_arc_points  = r_draw_arc_points;
    iface->fill_polygon     = r_fill_polygon;
    iface->fill_circle      = r_fill_circle;
    iface->stroke_circle    = r_stroke_circle;
    iface->draw_line        = r_draw_line;
    iface->set_clip_rect    = r_set_clip_rect;
    iface->clear_clip_rect  = r_clear_clip_rect;
    iface->get_pixels       = r_get_pixels;
    iface->put_pixels       = r_put_pixels;
    iface->to_data_url      = r_to_data_url;
    iface->present          = r_present;
    iface->clear_main       = r_clear_main;
    iface->get_time_ms      = r_get_time_ms;
    iface->sleep_ms         = r_sleep_ms;
}
