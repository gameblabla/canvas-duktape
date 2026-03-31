/*
 * Platform Abstraction Layer
 * 
 * Provides cross-platform file system and OS utilities.
 * Keeps backends (QuickJS, Duktape, etc.) portable.
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>

/* Get home directory path */
const char* platform_get_home_dir(void);

/* Create directory recursively */
int platform_mkdir_recursive(const char *path);

/* Get environment variable */
const char* platform_getenv(const char *name);

/* Storage file operations */
void platform_init_storage_path(const char *window_title);
const char* platform_get_storage_path(void);
int platform_load_storage(const char *path, char **out_content);
int platform_save_storage(const char *path, const char *json_content);

#endif /* PLATFORM_H */
