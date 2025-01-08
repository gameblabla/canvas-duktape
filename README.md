# Canvas Duktape

This is kind of a contiunation of Toy Browser except designed to run Javascript games that use the Canvas API.

The goal is ultimately to run simple Javascript games on embedded platforms like the GCW Zero, Bittboy, RS-97...
at much greater speeds than a web browser would on them and with a low lower RAM footprint.

I would like it to support simple HTML5 Canvas games.
This means some Game Maker studio games as well as older HTML5 games. (and newer games that still target Canvas)

# Current status

Works for test.html, test2.html and game.html.
game.html is Rubby Bird 1 HTML5 stripped down without the touch libraries and sound support.

No input, sound support.

# Roadmap

- This renders to intermediate texture right now to avoid flickering, this may have to be possibly tweaked later.
- Add support for more functions
- Add basic keyboard and mouse support
- Support Localstorage
- Support sound libraries directly such as buzz.js/howl.js through SDL_mixer
- SDL 1.2 backend for Opendingux handhelds (right now, SDL2 because its currently the best middleground)


# Far future
- Possibly WebGL support through ANGLE?
- ECMASCript 2015+ (Will possibly need upgrade to Duktape 3+)