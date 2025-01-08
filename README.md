# Canvas Duktape

This is kind of a contiunation of Toy Browser except designed to run Javascript games that use the Canvas API.

The goal is ultimately to run simple Javascript games on embedded platforms like the GCW Zero, Bittboy, RS-97...
at much greater speeds than a web browser would on them and with a low lower RAM footprint.

I would like it to support simple HTML5 Canvas games.
This means some Game Maker studio games as well as older HTML5 games. (and newer games that still target Canvas)

# Current status

Works for canvas_loop.js, canvas_variant2.js, rhino.js and canvas.js.
No input, sound support.
No support for loading from HTML file or references.
No support for changing resolution from HTML.

# Roadmap

- Support Localstorage
- Support sound libraries directly such as buzz.js/howl.js through SDL_mixer
- SDL 1.2 backend (right now, SDL2)


# Far future
- Possibly WebGL support through ANGLE?
- ECMASCript 2015+ (Will possibly need upgrade to Duktape 3+)