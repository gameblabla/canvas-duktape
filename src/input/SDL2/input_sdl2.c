#include <SDL2/SDL.h>
#include "input_sdl2.h"

static int sdl_key_to_browser_keycode(SDL_Keycode sym) {
    if (sym >= SDLK_a && sym <= SDLK_z)   return (sym - SDLK_a) + 65;
    if (sym >= SDLK_0 && sym <= SDLK_9)   return sym;
    if (sym >= SDLK_F1 && sym <= SDLK_F12) return 112 + (sym - SDLK_F1);
    if (sym >= SDLK_KP_0 && sym <= SDLK_KP_9) return 96 + (sym - SDLK_KP_0);
    switch (sym) {
        case SDLK_BACKSPACE:   return 8;
        case SDLK_TAB:         return 9;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:    return 13;
        case SDLK_PAUSE:       return 19;
        case SDLK_CAPSLOCK:    return 20;
        case SDLK_ESCAPE:      return 27;
        case SDLK_SPACE:       return 32;
        case SDLK_PAGEUP:      return 33;
        case SDLK_PAGEDOWN:    return 34;
        case SDLK_END:         return 35;
        case SDLK_HOME:        return 36;
        case SDLK_LEFT:        return 37;
        case SDLK_UP:          return 38;
        case SDLK_RIGHT:       return 39;
        case SDLK_DOWN:        return 40;
        case SDLK_INSERT:      return 45;
        case SDLK_DELETE:      return 46;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:      return 16;
        case SDLK_LCTRL:
        case SDLK_RCTRL:       return 17;
        case SDLK_LALT:
        case SDLK_RALT:        return 18;
        case SDLK_KP_MULTIPLY: return 106;
        case SDLK_KP_PLUS:     return 107;
        case SDLK_KP_MINUS:    return 109;
        case SDLK_KP_PERIOD:   return 110;
        case SDLK_KP_DIVIDE:   return 111;
        case SDLK_EQUALS:      return 187;
        case SDLK_COMMA:       return 188;
        case SDLK_MINUS:       return 189;
        case SDLK_PERIOD:      return 190;
        default:               return 0;
    }
}

static void i_init(void) { /* SDL already initialized by renderer */ }
static void i_quit(void) { /* SDL quit handled by renderer */ }

static int i_poll(InputEvent* out) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            out->type = INPUT_EVENT_QUIT;
            out->keycode = 0;
            return 1;
        }
        if (e.type == SDL_KEYDOWN) {
            int kc = sdl_key_to_browser_keycode(e.key.keysym.sym);
            if (kc) {
                out->type = INPUT_EVENT_KEYDOWN;
                out->keycode = kc;
                return 1;
            }
        }
        if (e.type == SDL_KEYUP) {
            int kc = sdl_key_to_browser_keycode(e.key.keysym.sym);
            if (kc) {
                out->type = INPUT_EVENT_KEYUP;
                out->keycode = kc;
                return 1;
            }
        }
        if (e.type == SDL_MOUSEMOTION) {
            out->type = INPUT_EVENT_MOUSEMOVE;
            out->x = e.motion.x;
            out->y = e.motion.y;
            out->button = 0;
            return 1;
        }
        if (e.type == SDL_MOUSEBUTTONDOWN) {
            out->type = INPUT_EVENT_MOUSEDOWN;
            out->x = e.button.x;
            out->y = e.button.y;
            out->button = (e.button.button == SDL_BUTTON_RIGHT) ? 2 :
                          (e.button.button == SDL_BUTTON_MIDDLE) ? 1 : 0;
            return 1;
        }
        if (e.type == SDL_MOUSEBUTTONUP) {
            out->type = INPUT_EVENT_MOUSEUP;
            out->x = e.button.x;
            out->y = e.button.y;
            out->button = (e.button.button == SDL_BUTTON_RIGHT) ? 2 :
                          (e.button.button == SDL_BUTTON_MIDDLE) ? 1 : 0;
            return 1;
        }
    }
    out->type = INPUT_EVENT_NONE;
    out->keycode = 0;
    return 0;
}

void input_sdl2_init_iface(InputInterface* iface) {
    iface->init = i_init;
    iface->quit = i_quit;
    iface->poll = i_poll;
}
