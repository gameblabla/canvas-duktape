# Canvas Duktape

This is kind of a contiunation of Toy Browser except designed to run Javascript games that use the Canvas API.

The goal is ultimately to run simple Javascript games on embedded platforms like the GCW Zero, Bittboy, RS-97...
at much greater speeds than a web browser would on them and with a low lower RAM footprint.

I would like it to support simple HTML5 Canvas games.
This means some Game Maker studio games as well as older HTML5 games. (and newer games that still target Canvas)

# Current status

Biolab Disaster is playable besides some graphical glitches on loading screen, audio is also properly supported in that game.
Derp Puncher is playable besides lacking touchscreen/mouse controls.
Other test examples are playable.

# TODO

- Date.now/performance.now support
- Input (Mouse, Touchscreen, Gamecontroller API)
- WebAudio (More advanced, used by more modern games)
- LocalStorage (Used for saves in games in particular)
- Add QuickJS-NG backend (the modular framework made this easier) as Duktape lacks proper EMCAScript 5/6 support that's very problematic already for some games
- Add SDL3 backend for renderer/sound/input (It's using SDL2 currently because that's what i was using then.)
- SDL1.2 backend (For older platforms like OpenDingux. Canvas API is easier to implement in software)
- Eventually when support gets further along, custom KOS PVR/Dreamcast support as separate backend (this is why the modular/opaque functions were important)
