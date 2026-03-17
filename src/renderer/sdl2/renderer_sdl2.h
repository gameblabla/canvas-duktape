#pragma once
#include "common/types.h"

/* Set resource directory for loading fonts and assets (call before init) */
void renderer_sdl2_set_resource_dir(const char* path);

/* Populate *iface with SDL2 renderer function pointers. */
void renderer_sdl2_init_iface(RendererInterface* iface);
