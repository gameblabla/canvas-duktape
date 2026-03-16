#pragma once
#include "common/types.h"

/* Populate *iface with SDL2 sound function pointers. */
void sound_sdl2_init_iface(SoundInterface* iface);
