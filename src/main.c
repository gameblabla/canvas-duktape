#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <ctype.h>

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
static char       g_window_title[512] = "Canvas Demo";  /* Default title */

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
    /* If it's an absolute path, data URL, or http(s) URL, return as-is */
    if (relative[0] == '/' || 
        strncmp(relative, "data:", 5) == 0 ||
        strncmp(relative, "http://", 7) == 0 ||
        strncmp(relative, "https://", 8) == 0) {
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
    int    inside_script  = 0;
    int    inside_img     = 0;
    int    inside_comment = 0;
    char   img_buffer[4096];
    char*  script_content      = NULL;
    size_t script_content_size = 0;
    size_t script_content_len  = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Strip HTML comments from non-script lines */
        if (!inside_script) {
            /* Handle multi-line comments with inside_comment flag */
            char stripped[4096];
            char *src = line, *dst = stripped;
            while (*src) {
                if (inside_comment) {
                    char *end = strstr(src, "-->");
                    if (end) {
                        inside_comment = 0;
                        src = end + 3;
                    } else {
                        break; /* rest of line is inside comment */
                    }
                } else {
                    char *start = strstr(src, "<!--");
                    if (start) {
                        /* copy up to comment start */
                        while (src < start) *dst++ = *src++;
                        inside_comment = 1;
                        src = start + 4;
                    } else {
                        /* no comment start - copy rest */
                        while (*src) *dst++ = *src++;
                    }
                }
            }
            *dst = '\0';
            strncpy(line, stripped, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
        }

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

            /* Parse generic elements with id and inline text content (e.g. <div id="x">text</div>) */
            {
                /* Check for an opening tag with an id attribute on this line */
                char elem_id[64] = {0};
                if (extract_attribute(line, "id", elem_id, sizeof(elem_id)) && elem_id[0]) {
                    /* Not a canvas, img, or script tag — extract text content between > and </ */
                    const char *tag_end = strchr(line, '>');
                    const char *close_start = strstr(line, "</");
                    if (tag_end && close_start && close_start > tag_end) {
                        char inner[512] = {0};
                        size_t len = (size_t)(close_start - (tag_end + 1));
                        if (len > 0 && len < sizeof(inner)) {
                            strncpy(inner, tag_end + 1, len);
                            inner[len] = '\0';
                            jscore_qjs_register_element(elem_id, inner);
                        }
                    }
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

            /* Parse title tag */
            if (strstr(line, "<title>") != NULL) {
                const char *start = strstr(line, "<title>") + 7;
                const char *end = strstr(start, "</title>");
                if (end && end > start) {
                    size_t len = (size_t)(end - start);
                    if (len > 0 && len < sizeof(g_window_title) - 1) {
                        strncpy(g_window_title, start, len);
                        g_window_title[len] = '\0';
                    }
                }
            }
        }
    }

    fclose(f);

    /* Use HTML title for localStorage path if available */
    fprintf(stderr, "[main] Window title: %s\n", g_window_title);
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

/* Global flag to disable WebAudio API (for debugging) */
int g_disable_webaudio = 0;

/* Global flag to enable broken/incomplete WebGL support (for testing) */
static int g_broken_webgl = 0;

/* Global flag to enable FPS counter display */
static int g_show_fps = 0;

/* Global screenshot delay in seconds (0 = disabled, uses frame-based screenshots) */
static int g_screenshot_delay = 0;

/* Global flag to enable input recording mode */
static int g_record_mode = 0;

/* ============================================================================
 * Input command system
 * ============================================================================ */
#define MAX_INPUT_COMMANDS 64

typedef enum {
    INPUT_CMD_MOUSE,
    INPUT_CMD_KEY
} InputCmdType;

typedef struct {
    InputCmdType type;
    double       delay_sec;
    int          keycode;       /* For keyboard: virtual key code */
    int          mouse_x;       /* For mouse: x coordinate */
    int          mouse_y;       /* For mouse: y coordinate */
    int          executed;
} InputCommand;

static InputCommand g_input_commands[MAX_INPUT_COMMANDS];
static int          g_input_command_count = 0;
static char         g_input_commands_raw[2048] = {0};

/* ============================================================================
 * Input recording (record mode)
 * ============================================================================ */
#define MAX_RECORDED_EVENTS 512

typedef enum {
    REC_KEY_DOWN,
    REC_KEY_UP,
    REC_MOUSE_DOWN,
    REC_MOUSE_UP
} RecEventType;

typedef struct {
    RecEventType type;
    double       time_sec;
    int          keycode;
    int          mouse_x;
    int          mouse_y;
    int          button;
} RecordedEvent;

static RecordedEvent g_recorded_events[MAX_RECORDED_EVENTS];
static int           g_recorded_count = 0;
static double        g_record_start_time = 0.0;

static void record_event(RecEventType type, double time_sec, int keycode, int mx, int my, int button) {
    if (g_recorded_count >= MAX_RECORDED_EVENTS) return;
    RecordedEvent* ev = &g_recorded_events[g_recorded_count++];
    ev->type = type;
    ev->time_sec = time_sec;
    ev->keycode = keycode;
    ev->mouse_x = mx;
    ev->mouse_y = my;
    ev->button = button;
}

static void print_recorded_commands(void) {
    if (g_recorded_count == 0) {
        fprintf(stderr, "[record] No events recorded.\n");
        return;
    }

    fprintf(stderr, "\n========================================\n");
    fprintf(stderr, "  Recorded Input Commands\n");
    fprintf(stderr, "========================================\n");
    fprintf(stderr, "--input-commands \"");

    int first = 1;
    for (int i = 0; i < g_recorded_count; i++) {
        RecordedEvent* ev = &g_recorded_events[i];
        if (!first) fprintf(stderr, ":");
        first = 0;

        if (ev->type == REC_KEY_DOWN) {
            fprintf(stderr, "k%d,s%.2f", ev->keycode, ev->time_sec);
        } else if (ev->type == REC_MOUSE_DOWN) {
            fprintf(stderr, "m,s%.2f,%d,%d", ev->time_sec, ev->mouse_x, ev->mouse_y);
        }
        /* Only record keydown and mousedown for cleaner output */
    }

    fprintf(stderr, "\"\n");
    fprintf(stderr, "========================================\n\n");
}

/* Parse a single command like "m,s10,400,350" or "k47,s10" */
static int parse_input_command(const char* cmd_str, InputCommand* cmd) {
    const char* p = cmd_str;

    /* Skip whitespace */
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) return 0;

    if (*p == 'm') {
        cmd->type = INPUT_CMD_MOUSE;
        p++;
        /* Skip comma */
        if (*p == ',') p++;
    } else if (*p == 'k') {
        cmd->type = INPUT_CMD_KEY;
        p++;
        /* Parse keycode */
        cmd->keycode = 0;
        while (*p && isdigit((unsigned char)*p)) {
            cmd->keycode = cmd->keycode * 10 + (*p - '0');
            p++;
        }
        if (*p == ',') p++;
    } else {
        fprintf(stderr, "[input-cmd] Unknown command type: '%c' in '%s'\n", *p, cmd_str);
        return 0;
    }

    /* Parse delay: s<number> */
    if (*p == 's') {
        p++;
        cmd->delay_sec = 0;
        int has_decimal = 0;
        double frac = 0.1;
        while (*p && (isdigit((unsigned char)*p) || *p == '.')) {
            if (*p == '.') {
                has_decimal = 1;
                p++;
                continue;
            }
            int digit = *p - '0';
            if (!has_decimal) {
                cmd->delay_sec = cmd->delay_sec * 10 + digit;
            } else {
                cmd->delay_sec += digit * frac;
                frac *= 0.1;
            }
            p++;
        }
        if (*p == ',') p++;
    }

    /* Parse mouse coordinates if mouse command */
    if (cmd->type == INPUT_CMD_MOUSE) {
        cmd->mouse_x = 0;
        while (*p && isdigit((unsigned char)*p)) {
            cmd->mouse_x = cmd->mouse_x * 10 + (*p - '0');
            p++;
        }
        if (*p == ',') p++;
        cmd->mouse_y = 0;
        while (*p && isdigit((unsigned char)*p)) {
            cmd->mouse_y = cmd->mouse_y * 10 + (*p - '0');
            p++;
        }
    }

    return 1;
}

static void parse_input_commands_string(const char* str) {
    char buf[256];
    const char* p = str;
    const char* start = str;

    g_input_command_count = 0;

    while (*p) {
        if (*p == ':') {
            size_t len = (size_t)(p - start);
            if (len > 0 && len < sizeof(buf)) {
                strncpy(buf, start, len);
                buf[len] = '\0';
                if (g_input_command_count < MAX_INPUT_COMMANDS) {
                    if (parse_input_command(buf, &g_input_commands[g_input_command_count])) {
                        g_input_command_count++;
                    }
                }
            }
            start = p + 1;
        }
        p++;
    }
    /* Last command */
    size_t len = (size_t)(p - start);
    if (len > 0 && len < sizeof(buf)) {
        strncpy(buf, start, len);
        buf[len] = '\0';
        if (g_input_command_count < MAX_INPUT_COMMANDS) {
            if (parse_input_command(buf, &g_input_commands[g_input_command_count])) {
                g_input_command_count++;
            }
        }
    }

    fprintf(stderr, "[input-cmd] Parsed %d input commands\n", g_input_command_count);
    for (int i = 0; i < g_input_command_count; i++) {
        if (g_input_commands[i].type == INPUT_CMD_MOUSE) {
            fprintf(stderr, "[input-cmd]   [%d] MOUSE at (%d,%d) after %.1fs\n",
                    i, g_input_commands[i].mouse_x, g_input_commands[i].mouse_y,
                    g_input_commands[i].delay_sec);
        } else {
            fprintf(stderr, "[input-cmd]   [%d] KEY code=%d after %.1fs\n",
                    i, g_input_commands[i].keycode, g_input_commands[i].delay_sec);
        }
    }
}

int main(int argc, char** argv) {
    /* Parse command-line arguments */
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [--no-webaudio] [--broken-webgl] [--fps] [-s <seconds>] [--input-commands <cmds>] [--record] <file.html>\n", argv[0]);
        return 1;
    }

    const char* html_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-webaudio") == 0) {
            g_disable_webaudio = 1;
            fprintf(stderr, "[main] WebAudio API disabled via command line\n");
        } else if (strcmp(argv[i], "--broken-webgl") == 0) {
            g_broken_webgl = 1;
            fprintf(stderr, "[main] Broken WebGL support enabled via command line\n");
        } else if (strcmp(argv[i], "--fps") == 0) {
            g_show_fps = 1;
            fprintf(stderr, "[main] FPS counter enabled\n");
        } else if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 < argc) {
                g_screenshot_delay = atoi(argv[i + 1]);
                fprintf(stderr, "[main] Screenshot scheduled after %d seconds\n", g_screenshot_delay);
                i++;
            } else {
                fprintf(stderr, "Error: -s requires a value (seconds)\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--input-commands") == 0) {
            if (i + 1 < argc) {
                strncpy(g_input_commands_raw, argv[i + 1], sizeof(g_input_commands_raw) - 1);
                parse_input_commands_string(argv[i + 1]);
                i++;
            } else {
                fprintf(stderr, "Error: --input-commands requires a value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--record") == 0) {
            g_record_mode = 1;
            fprintf(stderr, "[main] Input recording mode enabled\n");
        } else {
            html_path = argv[i];
        }
    }

    if (!html_path) {
        fprintf(stderr, "Usage: %s [--no-webaudio] [--broken-webgl] [--fps] <file.html>\n", argv[0]);
        return 1;
    }

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

    /* Set base directory for XHR (for resolving relative file paths in scripts) */
    jscore_qjs_set_base_dir(g_base_dir);

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

    /* Enable broken WebGL if requested */
    if (jscore.set_broken_webgl) {
        jscore.set_broken_webgl(g_broken_webgl);
    }

    /* --- Set up JS globals (document, window, canvas elements, …) --- */
    jscore.setup_globals(win_w, win_h,
                         g_canvas_info, g_canvas_count,
                         g_image_info,  g_image_count,
                         g_window_title);

    /* --- Execute scripts in document order (inline and external interleaved) --- */
    fprintf(stderr, "[main] Total scripts: %d\n", g_script_count);
    for (int i = 0; i < g_script_count; i++) {
        fprintf(stderr, "[main] Script %d: src='%s', is_inline=%d\n",
                i, g_script_info[i].src, g_script_info[i].is_inline);
        if (g_script_info[i].is_inline) {
            if (g_script_info[i].inline_code) {
                fprintf(stderr, "[main] Executing inline script %d...\n", i);
                if (jscore.eval_string(g_script_info[i].inline_code))
                    fprintf(stderr, "[main] Inline script %d executed successfully\n", i);
                else
                    fprintf(stderr, "Inline script %d error\n", i);
            }
        } else if (g_script_info[i].src[0] != '\0') {
            if (jscore.eval_file(g_script_info[i].src))
                fprintf(stderr, "[main] Loaded script: %s\n", g_script_info[i].src);
        }
    }
    
    /* Pre-initialize mouse variables for games that need them (derp_puncher) */
    jscore.eval_string("if(typeof mousex==='undefined'){mousex=0;mousey=0;mousse=0;}");
    fprintf(stderr, "[main] Initialized mouse variables\n");
    fprintf(stderr, "[main] Before canvas setup, g_canvas_count=%d\n", g_canvas_count);

    /* --- Preload HTML images (set .src to trigger onload) --- */
    jscore.preload_images(g_image_info, g_image_count);

    /* --- Fire load events --- */
    jscore.call_window_load_listeners();
    jscore.call_window_onload();

    /* Start recording timer */
    if (g_record_mode) {
        g_record_start_time = renderer.get_time_ms();
    }

    /* --- Main loop --- */
    int running = 1;
    int frame_count = 0;
    int time_screenshot_taken = 0;

    /* FPS counter variables */
    int fps_frame_count = 0;
    double fps_last_time = renderer.get_time_ms();
    int fps = 0;

    while (running) {
        InputEvent ev;
        while (input.poll(&ev)) {
            if (ev.type == INPUT_EVENT_QUIT) {
                running = 0;
            } else if (ev.type == INPUT_EVENT_WINDOW_RESIZE) {
                /* Handle user resizing the window - just update logical size for scaling */
                if (renderer.handle_window_resize) {
                    renderer.handle_window_resize(ev.x, ev.y);
                }
                /* Don't update canvas size - let game render at its native resolution */
            } else if (ev.type == INPUT_EVENT_KEYDOWN) {
                jscore.dispatch_key(ev.keycode, 1);
                if (g_record_mode) {
                    double t = (renderer.get_time_ms() - g_record_start_time) / 1000.0;
                    record_event(REC_KEY_DOWN, t, ev.keycode, 0, 0, 0);
                }
            } else if (ev.type == INPUT_EVENT_KEYUP) {
                jscore.dispatch_key(ev.keycode, 0);
            } else if (ev.type == INPUT_EVENT_MOUSEMOVE ||
                       ev.type == INPUT_EVENT_MOUSEDOWN ||
                       ev.type == INPUT_EVENT_MOUSEUP) {
                jscore.dispatch_mouse(ev.type, ev.x, ev.y, ev.button);
                if (g_record_mode) {
                    double t = (renderer.get_time_ms() - g_record_start_time) / 1000.0;
                    if (ev.type == INPUT_EVENT_MOUSEDOWN) {
                        record_event(REC_MOUSE_DOWN, t, 0, ev.x, ev.y, ev.button);
                    }
                }
            } else if (ev.type == INPUT_EVENT_TOUCHMOVE ||
                       ev.type == INPUT_EVENT_TOUCHDOWN ||
                       ev.type == INPUT_EVENT_TOUCHUP) {
                jscore.dispatch_touch(ev.type, ev.x, ev.y, ev.touch_id);
            }
        }

        frame_count++;
        fps_frame_count++;

        /* Execute input commands based on elapsed time */
        if (g_input_command_count > 0) {
            double elapsed_ms = renderer.get_time_ms();
            for (int i = 0; i < g_input_command_count; i++) {
                if (g_input_commands[i].executed) continue;
                if (elapsed_ms < g_input_commands[i].delay_sec * 1000.0) continue;

                if (g_input_commands[i].type == INPUT_CMD_MOUSE) {
                    char mouse_cmd[256];
                    snprintf(mouse_cmd, sizeof(mouse_cmd),
                             "if(typeof mousex!=='undefined'){mousex=%d;mousey=%d;mousse=1;}",
                             g_input_commands[i].mouse_x, g_input_commands[i].mouse_y);
                    jscore.eval_string(mouse_cmd);
                    jscore.dispatch_mouse(INPUT_EVENT_MOUSEDOWN,
                                          g_input_commands[i].mouse_x,
                                          g_input_commands[i].mouse_y, 0);
                    fprintf(stderr, "[input-cmd] Mouse pressed at (%d,%d)\n",
                            g_input_commands[i].mouse_x, g_input_commands[i].mouse_y);
                    /* Release after a short hold */
                    jscore.dispatch_mouse(INPUT_EVENT_MOUSEUP,
                                          g_input_commands[i].mouse_x,
                                          g_input_commands[i].mouse_y, 0);
                    fprintf(stderr, "[input-cmd] Mouse released\n");
                } else {
                    jscore.dispatch_key(g_input_commands[i].keycode, 1);
                    jscore.dispatch_key(g_input_commands[i].keycode, 0);
                    fprintf(stderr, "[input-cmd] Key %d pressed and released\n",
                            g_input_commands[i].keycode);
                }
                g_input_commands[i].executed = 1;
            }
        }

        jscore.check_timers();

        /* Calculate FPS every second */
        double current_time = renderer.get_time_ms();
        if (current_time - fps_last_time >= 1000.0) {
            fps = fps_frame_count;
            fps_frame_count = 0;
            fps_last_time = current_time;
        }

        /* Render FPS counter if enabled */
        if (g_show_fps && renderer.fill_text) {
            char fps_text[32];
            snprintf(fps_text, sizeof(fps_text), "FPS: %d", fps);
            /* Draw FPS in top-left corner with white text, black outline */
            renderer.fill_text(renderer.get_main_texture(), fps_text, 2, 14,
                              0, 0, 0, 255, 12, TEXT_ALIGN_LEFT, TEXT_BASELINE_TOP, "monospace");
            renderer.fill_text(renderer.get_main_texture(), fps_text, 1, 13,
                              255, 255, 255, 255, 12, TEXT_ALIGN_LEFT, TEXT_BASELINE_TOP, "monospace");
        }
        
        /* Time-based screenshot via -s flag */
        if (g_screenshot_delay > 0 && !time_screenshot_taken) {
            double elapsed_ms = renderer.get_time_ms();
            if (elapsed_ms >= (double)g_screenshot_delay * 1000.0) {
                char screenshot_path[512];
                snprintf(screenshot_path, sizeof(screenshot_path), "screenshot_%ds.bmp", g_screenshot_delay);
                if (renderer.screenshot(screenshot_path) == 0) {
                    fprintf(stderr, "[screenshot] Saved %s (after %d seconds)\n", screenshot_path, g_screenshot_delay);
                } else {
                    fprintf(stderr, "[screenshot] Failed to save %s\n", screenshot_path);
                }
                time_screenshot_taken = 1;
            }
        }

        renderer.present();
    }

    /* --- Cleanup --- */
    jscore.quit();
    input.quit();
    sound.quit();
    renderer.quit();

    /* Print recorded commands if in record mode */
    if (g_record_mode) {
        print_recorded_commands();
    }

    /* Free inline script memory */
    for (int i = 0; i < g_script_count; i++) {
        if (g_script_info[i].is_inline && g_script_info[i].inline_code) {
            free(g_script_info[i].inline_code);
            g_script_info[i].inline_code = NULL;
        }
    }

    return 0;
}
