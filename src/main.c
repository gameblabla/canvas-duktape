#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>

#include "common/types.h"
#include "renderer/sdl2/renderer_sdl2.h"
#include "input/SDL2/input_sdl2.h"
#include "sound/SDL2/sound_sdl2.h"

/* Conditional include based on JS backend */
#if JSCORE_BACKEND_QUICKJS
#include "jscore/quickjs/jscore_qjs.h"
#else
#include "jscore/duktape/jscore_duk.h"
#endif

/* ============================================================================
 * HTML parse state (filled by parse_html, consumed by main)
 * ============================================================================ */
static CanvasInfo g_canvas_info[MAX_IMAGES];
static int        g_canvas_count = 0;
static ImageInfo  g_image_info[MAX_IMAGES];
static int        g_image_count  = 0;
static ScriptInfo g_script_info[MAX_SCRIPTS];
static int        g_script_count = 0;

/* ============================================================================
 * Base directory for resolving relative paths (set from HTML file path)
 * ============================================================================ */
static char g_base_dir[1024] = {0};
static char g_original_cwd[1024] = {0};

/* ============================================================================
 * Helper: extract a quoted attribute value from an HTML tag string
 * ============================================================================ */
static int extract_attribute(const char* tag, const char* attr,
                              char* value, size_t size) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "%s=\"", attr);
    const char* start = strstr(tag, pattern);
    if (!start) return 0;
    start += strlen(pattern);
    const char* end = strchr(start, '"');
    if (!end) return 0;
    size_t len = (size_t)(end - start);
    if (len >= size) len = size - 1;
    strncpy(value, start, len);
    value[len] = '\0';
    return 1;
}

/* ============================================================================
 * Helper: resolve a relative path against the base directory
 * ============================================================================ */
static void resolve_relative_path(const char* relative, char* out, size_t out_size) {
    /* If it's an absolute path or data URL, return as-is */
    if (relative[0] == '/' || strncmp(relative, "data:", 5) == 0) {
        strncpy(out, relative, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    /* If no base dir set, return as-is */
    if (g_base_dir[0] == '\0') {
        strncpy(out, relative, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    /* Combine base dir with relative path */
    snprintf(out, out_size, "%s/%s", g_base_dir, relative);
}

/* ============================================================================
 * HTML Parsing (handles multi-line tags and inline scripts)
 * ============================================================================ */
static int parse_html(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Failed to open HTML file: %s\n", path);
        return 0;
    }

    char   line[4096];
    int    inside_script = 0;
    int    inside_img    = 0;
    char   img_buffer[4096];
    char*  script_content      = NULL;
    size_t script_content_size = 0;
    size_t script_content_len  = 0;

    while (fgets(line, sizeof(line), f)) {
        if (inside_script) {
            /* Check for closing script tag */
            if (strstr(line, "</script>") != NULL) {
                char*  end = strstr(line, "</script>");
                size_t len = (size_t)(end - line);
                if (len > 0) {
                    if (script_content_len + len + 1 > script_content_size) {
                        script_content_size = script_content_len + len + 1024;
                        script_content = realloc(script_content, script_content_size);
                    }
                    strncpy(script_content + script_content_len, line, len);
                    script_content_len += len;
                    script_content[script_content_len] = '\0';
                }
                if (g_script_count < MAX_SCRIPTS && script_content_len > 0) {
                    ScriptInfo* script = &g_script_info[g_script_count];
                    script->is_inline   = 1;
                    script->inline_code = script_content;
                    script->src[0]      = '\0';
                    g_script_count++;
                    fprintf(stderr, "[parse_html] Found inline script (%zu bytes)\n",
                            script_content_len);
                } else {
                    free(script_content);
                }
                script_content      = NULL;
                script_content_size = 0;
                script_content_len  = 0;
                inside_script       = 0;
            } else {
                size_t len = strlen(line);
                if (script_content_len + len + 1 > script_content_size) {
                    script_content_size = script_content_len + len + 1024;
                    script_content = realloc(script_content, script_content_size);
                }
                strcpy(script_content + script_content_len, line);
                script_content_len += len;
            }
        } else {
            /* Check for opening script tag */
            if (strstr(line, "<script") != NULL) {
                char src_value[512];
                int  has_src = extract_attribute(line, "src", src_value, sizeof(src_value));
                if (has_src && src_value[0] != '\0') {
                    if (g_script_count < MAX_SCRIPTS) {
                        ScriptInfo* script = &g_script_info[g_script_count];
                        script->is_inline   = 0;
                        script->inline_code = NULL;
                        /* Resolve relative path against HTML file directory */
                        resolve_relative_path(src_value, script->src, sizeof(script->src));
                        g_script_count++;
                        fprintf(stderr, "[parse_html] Found external script: %s\n", script->src);
                    }
                } else {
                    inside_script       = 1;
                    script_content      = NULL;
                    script_content_size = 0;
                    script_content_len  = 0;

                    char* tag_end = strstr(line, ">");
                    if (tag_end != NULL && strlen(tag_end) > 1) {
                        size_t len = strlen(tag_end + 1);
                        if (len > 0) {
                            script_content_size = len + 1024;
                            script_content      = malloc(script_content_size);
                            strcpy(script_content, tag_end + 1);
                            script_content_len = len;
                        }
                    }

                    if (strstr(line, "</script>") != NULL) {
                        inside_script = 0;
                        if (script_content) {
                            char* end = strstr(script_content, "</script>");
                            if (end != NULL) *end = '\0';
                            if (strlen(script_content) > 0 && g_script_count < MAX_SCRIPTS) {
                                ScriptInfo* script = &g_script_info[g_script_count];
                                script->is_inline   = 1;
                                script->inline_code = script_content;
                                script->src[0]      = '\0';
                                g_script_count++;
                                fprintf(stderr, "[parse_html] Found inline script (%zu bytes)\n",
                                        strlen(script_content));
                            } else {
                                free(script_content);
                            }
                            script_content = NULL;
                        }
                    }
                }
            }

            /* Parse canvas tags */
            if (strstr(line, "<canvas") != NULL) {
                if (g_canvas_count < MAX_IMAGES) {
                    CanvasInfo* c = &g_canvas_info[g_canvas_count];
                    extract_attribute(line, "id", c->id, sizeof(c->id));
                    char width_str[32], height_str[32];
                    if (extract_attribute(line, "width", width_str, sizeof(width_str)))
                        c->width = atoi(width_str);
                    if (extract_attribute(line, "height", height_str, sizeof(height_str)))
                        c->height = atoi(height_str);
                    extract_attribute(line, "style", c->style, sizeof(c->style));
                    g_canvas_count++;
                }
            }

            /* Parse img tags (may be multi-line) */
            if (inside_img) {
                strcat(img_buffer, line);
                if (strstr(line, ">") != NULL || strstr(line, "/>") != NULL) {
                    inside_img = 0;
                    if (g_image_count < MAX_IMAGES) {
                        ImageInfo* img = &g_image_info[g_image_count];
                        char src_value[512];
                        extract_attribute(img_buffer, "id",  img->id,  sizeof(img->id));
                        extract_attribute(img_buffer, "src", src_value, sizeof(src_value));
                        /* Resolve relative path against HTML file directory */
                        resolve_relative_path(src_value, img->src, sizeof(img->src));
                        char width_str[32], height_str[32];
                        if (extract_attribute(img_buffer, "width",  width_str,  sizeof(width_str)))
                            img->width  = atoi(width_str);
                        if (extract_attribute(img_buffer, "height", height_str, sizeof(height_str)))
                            img->height = atoi(height_str);
                        g_image_count++;
                    }
                    img_buffer[0] = '\0';
                }
            } else if (strstr(line, "<img") != NULL) {
                if (strstr(line, ">") != NULL || strstr(line, "/>") != NULL) {
                    if (g_image_count < MAX_IMAGES) {
                        ImageInfo* img = &g_image_info[g_image_count];
                        char src_value[512];
                        extract_attribute(line, "id",  img->id,  sizeof(img->id));
                        extract_attribute(line, "src", src_value, sizeof(src_value));
                        /* Resolve relative path against HTML file directory */
                        resolve_relative_path(src_value, img->src, sizeof(img->src));
                        char width_str[32], height_str[32];
                        if (extract_attribute(line, "width",  width_str,  sizeof(width_str)))
                            img->width  = atoi(width_str);
                        if (extract_attribute(line, "height", height_str, sizeof(height_str)))
                            img->height = atoi(height_str);
                        g_image_count++;
                    }
                } else {
                    inside_img = 1;
                    strcpy(img_buffer, line);
                }
            }
        }
    }

    fclose(f);
    if (script_content) free(script_content);

    /* Log parsed images */
    printf("Parsed Images:\n");
    fflush(stdout);
    for (int i = 0; i < g_image_count; i++) {
        printf("  Image %d: id='%s', src='%s', width=%d, height=%d\n",
               i, g_image_info[i].id, g_image_info[i].src,
               g_image_info[i].width, g_image_info[i].height);
    }
    fflush(stdout);
    return 1;
}

/* ============================================================================
 * Helper: get the directory containing the executable
 * Uses argv[0] with realpath() for portability
 * Includes fallbacks for platforms without realpath/getcwd
 * ============================================================================ */
static void get_exe_dir(const char* argv0, char* out, size_t out_size) {
    char resolved[PATH_MAX];
    char* dir;
    
    /* Try realpath first (handles symlinks and relative paths) */
#if defined(HAVE_REALPATH) || !defined(__STRICT_ANSI__)
    if (realpath(argv0, resolved) != NULL) {
        dir = dirname(resolved);
        strncpy(out, dir, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
#else
    (void)resolved;
#endif
    
    /* Fallback: if argv[0] contains a path separator, extract directory */
    if (strchr(argv0, '/') != NULL) {
        char* argv0_copy = strdup(argv0);
        if (argv0_copy) {
            dir = dirname(argv0_copy);
            strncpy(out, dir, out_size - 1);
            out[out_size - 1] = '\0';
            free(argv0_copy);
            return;
        }
    }
    
    /* Last resort: use current directory if available */
#if defined(HAVE_GETCWD) || !defined(__STRICT_ANSI__)
    if (getcwd(out, out_size) != NULL) {
        return;
    }
#endif
    
    /* Ultimate fallback: use "." */
    strncpy(out, ".", out_size - 1);
    out[out_size - 1] = '\0';
}

/* ============================================================================
 * Main entry point
 * ============================================================================ */
int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.html>\n", argv[0]);
        return 1;
    }
    const char* html_path = argv[1];

    /* --- Get executable directory for resource files (fonts, etc.) --- */
    get_exe_dir(argv[0], g_original_cwd, sizeof(g_original_cwd));
    
    /* Extract directory from HTML path */
    char* html_path_copy = strdup(html_path);
    if (html_path_copy) {
        char* dir = dirname(html_path_copy);
        if (dir && strcmp(dir, ".") != 0) {
            strncpy(g_base_dir, dir, sizeof(g_base_dir) - 1);
        }
        free(html_path_copy);
    }

    /* --- Parse HTML --- */
    memset(g_canvas_info, 0, sizeof(g_canvas_info));
    g_canvas_count = 0;
    g_image_count  = 0;
    g_script_count = 0;
    if (!parse_html(html_path)) {
        fprintf(stderr, "Failed to parse HTML file.\n");
        return 1;
    }

    /* --- Determine window dimensions from first canvas --- */
    int win_w = 120, win_h = 160;
    if (g_canvas_count > 0 && g_canvas_info[0].width  > 0) win_w = g_canvas_info[0].width;
    if (g_canvas_count > 0 && g_canvas_info[0].height > 0) win_h = g_canvas_info[0].height;

    /* --- Initialise backends --- */
    RendererInterface renderer;
    InputInterface    input;
    SoundInterface    sound;
    JSCoreInterface   jscore;

    /* Set resource directory for renderer (fonts, etc.) */
    renderer_sdl2_set_resource_dir(g_original_cwd);

    renderer_sdl2_init_iface(&renderer);
    input_sdl2_init_iface(&input);
    sound_sdl2_init_iface(&sound);
#if JSCORE_BACKEND_QUICKJS
    jscore_qjs_init_iface(&jscore);
#else
    jscore_duk_init_iface(&jscore);
#endif

    /* Set base directory for sound (for resolving relative audio paths) */
    sound_set_base_dir(g_base_dir);

    if (!renderer.init(win_w, win_h, "Canvas Demo")) {
        fprintf(stderr, "Renderer init failed.\n");
        return 1;
    }
    sound.init();
    input.init();

    if (!jscore.init(&renderer, &input, &sound)) {
        fprintf(stderr, "JS core init failed.\n");
        renderer.quit();
        return 1;
    }

    /* --- Set up JS globals (document, window, canvas elements, …) --- */
    jscore.setup_globals(win_w, win_h,
                         g_canvas_info, g_canvas_count,
                         g_image_info,  g_image_count);

    /* --- Execute external scripts first --- */
    fprintf(stderr, "[main] Total scripts: %d\n", g_script_count);
    for (int i = 0; i < g_script_count; i++) {
        fprintf(stderr, "[main] Script %d: src='%s', is_inline=%d\n",
                i, g_script_info[i].src, g_script_info[i].is_inline);
        if (g_script_info[i].is_inline || g_script_info[i].src[0] == '\0')
            continue;
        if (jscore.eval_file(g_script_info[i].src))
            fprintf(stderr, "[main] Loaded script: %s\n", g_script_info[i].src);
    }

    /* --- Execute inline scripts --- */
    fprintf(stderr, "[main] Before inline scripts, g_canvas_count=%d\n", g_canvas_count);
    for (int i = 0; i < g_script_count; i++) {
        if (g_script_info[i].is_inline && g_script_info[i].inline_code) {
            fprintf(stderr, "[main] Executing inline script %d...\n", i);
            if (jscore.eval_string(g_script_info[i].inline_code))
                fprintf(stderr, "[main] Inline script %d executed successfully\n", i);
            else
                fprintf(stderr, "Inline script %d error\n", i);
        }
    }

    /* --- Preload HTML images (set .src to trigger onload) --- */
    jscore.preload_images(g_image_info, g_image_count);

    /* --- Fire load events --- */
    jscore.call_window_load_listeners();
    jscore.call_window_onload();

    /* --- Main loop --- */
    int running = 1;
    while (running) {
        InputEvent ev;
        while (input.poll(&ev)) {
            if (ev.type == INPUT_EVENT_QUIT) {
                running = 0;
            } else if (ev.type == INPUT_EVENT_KEYDOWN) {
                jscore.dispatch_key(ev.keycode, 1);
            } else if (ev.type == INPUT_EVENT_KEYUP) {
                jscore.dispatch_key(ev.keycode, 0);
            } else if (ev.type == INPUT_EVENT_MOUSEMOVE ||
                       ev.type == INPUT_EVENT_MOUSEDOWN ||
                       ev.type == INPUT_EVENT_MOUSEUP) {
                jscore.dispatch_mouse(ev.type, ev.x, ev.y, ev.button);
            }
        }

        jscore.check_timers();
        renderer.present();
        renderer.sleep_ms(10);
    }

    /* --- Cleanup --- */
    jscore.quit();
    input.quit();
    sound.quit();
    renderer.quit();

    /* Free inline script memory */
    for (int i = 0; i < g_script_count; i++) {
        if (g_script_info[i].is_inline && g_script_info[i].inline_code) {
            free(g_script_info[i].inline_code);
            g_script_info[i].inline_code = NULL;
        }
    }

    return 0;
}
