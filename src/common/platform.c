/*
 * Platform Abstraction Layer - POSIX Implementation
 */

#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char g_storage_path[1024] = {0};

const char* platform_get_home_dir(void) {
    const char *home = getenv("HOME");
    if (!home) home = getenv("USERPROFILE");
    if (!home) home = ".";
    return home;
}

int platform_mkdir_recursive(const char *path) {
    char dir_path[1024];
    strncpy(dir_path, path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    
    /* Create directories recursively */
    char *p = dir_path;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(dir_path, 0755);
            *p = '/';
        }
        p++;
    }
    return mkdir(dir_path, 0755);
}

const char* platform_getenv(const char *name) {
    return getenv(name);
}

void platform_init_storage_path(const char *window_title) {
    char safe_title[256] = {0};
    const char *base_dir = platform_getenv("CANVAS_STORAGE_DIR");
    char dir_path[1024];

    /* Create safe filename from window title */
    if (window_title && window_title[0]) {
        int j = 0;
        for (int i = 0; window_title[i] && j < (int)sizeof(safe_title) - 1; i++) {
            char c = window_title[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ') {
                safe_title[j++] = (c == ' ') ? '_' : c;
            }
        }
        safe_title[j] = '\0';
    }

    /* Fallback to filename if no title */
    if (safe_title[0] == '\0') {
        strncpy(safe_title, "default", sizeof(safe_title) - 1);
    }

    /* Build path: $HOME/.local/html5web/<safe_title>/localStorage.json */
    if (base_dir && base_dir[0]) {
        snprintf(g_storage_path, sizeof(g_storage_path), "%s/%s/localStorage.json",
                 base_dir, safe_title);
        snprintf(dir_path, sizeof(dir_path), "%s/%s", base_dir, safe_title);
    } else {
        snprintf(g_storage_path, sizeof(g_storage_path), "%s/.local/html5web/%s/localStorage.json",
                 platform_get_home_dir(), safe_title);
        snprintf(dir_path, sizeof(dir_path), "%s/.local/html5web/%s",
                 platform_get_home_dir(), safe_title);
    }

    /* Create directory recursively (NOT the file path!) */
    platform_mkdir_recursive(dir_path);
}

const char* platform_get_storage_path(void) {
    return g_storage_path;
}

int platform_load_storage(const char *path, char **out_content) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(f);
        return -1;
    }
    
    *out_content = malloc(size + 1);
    if (!*out_content) {
        fclose(f);
        return -1;
    }
    
    size_t read = fread(*out_content, 1, size, f);
    (*out_content)[read] = '\0';
    fclose(f);
    
    return (int)read;
}

int platform_save_storage(const char *path, const char *json_content) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    
    fprintf(f, "%s", json_content);
    fclose(f);
    return 0;
}
