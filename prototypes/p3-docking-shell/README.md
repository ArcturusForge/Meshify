# P3: Qt docking shell and layout recovery

Question from ADR-0001: can a Qt 6 shell with docking panels pass **UX-03** (import a layout from a disconnected monitor; panels stay accessible; shortcut changes are explicit)?

The shell uses Qt 6 Widgets and [Qt Advanced Docking System](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System) 4.4.1 (LGPL-2.1, fetched at configure time, built static). It has the default layout from the brief: viewport in the centre, object browser left, tool groups above, tool settings right, history and task status below. Panels dock, tab, resize and pop out into their own windows.

ADS saves and restores a layout, but it does not know about screens, plugins or shortcuts. `src/layout.cpp` adds:

| Need from the brief | What the layout code does |
| --- | --- |
| Recover panels from missing monitors | Saves the screens next to the layout. After a restore, any floating panel whose title bar is off every screen is moved onto the primary screen, and the user is told which panels moved. |
| Identify missing-plugin panels without breaking the workspace | A panel in the file that this build cannot make becomes a labelled placeholder in the same place. Saving again keeps it, so installing the plugin later finds its spot. |
| Do not silently replace controls | Import is two steps. `preview()` lists screens that are gone, unavailable panels and every shortcut change (added, changed, conflicting). `commit()` applies only the shortcut changes the caller accepts. Conflicting shortcuts are never applied. |
| Visible recovery route | `resetToDefault()` returns to the default arrangement. A layout whose saved state is damaged falls back to it. |

The layout file is JSON (`format: meshify-layout`, `version: 1`): name, screens, panels, floating panel frames, the ADS state, and shortcuts.

## Tests

`p3_docking_tests` runs 7 tests on Qt's offscreen platform, which is how CI runs it. Screens are simulated through `LayoutManager::setScreenProvider`.

| Test | Checks |
| --- | --- |
| rescue placement rules | Reachable, unreachable (off the edge, above the top, only 30 px of title bar visible), shrinking, cascading |
| default layout and reset | Six panels; reset closes every popped-out window |
| UX-03 layout from a disconnected monitor | Two panels saved on DP-2 are moved when DP-2 is gone; the panel on DP-1 stays; all three can be grabbed; none is lost |
| panel left off screen by the platform | The fallback moves a panel the platform left off screen |
| missing plugin panel keeps its place | Placeholder created and kept when the layout is saved again |
| shortcut preview is explicit | Added, changed, same and conflict; accepting nothing changes nothing; a conflict is reported and never applied |
| import leaves shortcuts alone until accepted | A committed import changes no shortcut that was not accepted |
| bad layout files are rejected | Garbage, a foreign format and a newer version are refused; a damaged state falls back to the default |

## Findings

1. **Qt already moves most restored windows onto a connected screen.** In the UX-03 test the panels saved on DP-2 were already back on screen when ADS finished restoring, before the fallback ran. The fallback is still needed for platforms or window managers that do not do this (the "left off screen by the platform" test), but the main value of the layout code is the **report**: Qt moves panels without saying so, and the brief asks the user to be told. The file therefore stores the saved frame of each floating panel so the report can name what moved.
2. **Linux builds need extra development files.** ADS includes `xcb/xcb.h` on Linux and Qt's private GUI headers. CI installs `libxcb` from conda-forge; Ubuntu needs `libxcb1-dev` and `qt6-base-private-dev`.
3. **Qt is now part of the CI environment** (`qt6-main` from conda-forge on all three systems). The environment is cached, so only the first run pays for it.

## Not proven here

The offscreen platform has one screen. These tests show the logic and that ADS restores and floats panels on all three systems in CI. They do **not** show how each window manager places windows when a real monitor is unplugged. That needs a two-monitor check on the reference machine:

1. Run `p3_shell`. Use *Layout > Save layout...* with a panel popped out onto the second monitor.
2. Close the app and unplug or disable the second monitor.
3. Run `p3_shell` again and use *Layout > Import layout...* with the saved file.
4. Expect a summary naming the missing screen, and the panel on your main screen with its title bar reachable.
5. Repeat on macOS and Linux (X11 and Wayland) when available. Wayland does not let applications choose window positions, so floating panels may open wherever the compositor puts them.

## Build and run

Needs Qt 6 and network access (to fetch ADS).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target p3_shell p3_docking_tests
./build/prototypes/p3-docking-shell/p3_shell
ctest --test-dir build -R p3
```

Without Qt 6 the prototype is skipped, so P1 and P2 still build.
