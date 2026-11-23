# Nocturne scene gallery

Four live terminal compositions inspired by the Nocturne RC5 Gallery. They use the current SDK's subcell canvas, Braille sampling, frame colors, compositing, tween sampler, input decoder, ANSI presenter, and analytic water-wave simulation. The OS provides the terminal and clock. The layouts, flight instrument data, and agent conversation are example code. The agent screen uses a scripted review session and accepts typed prompts.

Build against the installed **complete** profile and run in a POSIX terminal with UTF-8, Braille, true color, and at least 112 columns by 36 rows.

```sh
cmake -S examples/scene-gallery -B scene-build -DCMAKE_PREFIX_PATH=/path/to/nocturne-sdk
cmake --build scene-build
./scene-build/nocturne_scenes agents
```

- F1 opens Flight Deck. Left and Right change heading.
- F2 opens Tidal Console. SDK wave samples drive the water in the background.
- F3 opens Orbital Review. Press Enter to write and submit a prompt. Click an approval, or press A or D. Click a file, or press 1 through 3, to open its diff. Escape closes the diff.
- F4 opens Signal Desk. The scope and four signal levels animate independently.
- Q exits from the scene. Ctrl+C also exits and restores the terminal.

The [HTML gallery](../../docs/gallery/index.html) includes captured frames. The [visual coverage index](../../docs/gallery/COVERAGE.md) tracks component-level demonstrations and RC5 gaps.
