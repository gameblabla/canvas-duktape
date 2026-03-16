#include "sound_sdl2.h"

static void s_init(void) { /* no SDL_mixer for now */ }
static void s_quit(void) { }

void sound_sdl2_init_iface(SoundInterface* iface) {
    iface->init = s_init;
    iface->quit = s_quit;
}
