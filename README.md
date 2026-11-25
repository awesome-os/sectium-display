# sectium-display

A **4–21 KB** zero-dependency, cross-platform C11 display & actor-stream protocol for **Windows, Linux, macOS, and WASI** — paired with a hot-reloadable scriptable 2D/3D Window Manager and a **modular WebFrame Surface Provider**.

## Highlights
- **Truly Cross-Platform C11 ABI:** Compiles to Win64 `.dll` (`21.5 KB`), Linux `.so` (`6.0–6.7 KB`), macOS `.dylib` (`50.3 KB`), and `wasm32-wasi` (`4.25 KB`).
- **Modular WebFrame Renderer inside the WM (`wm/webframe-provider.mts`):** Mount any web page as a native `kind: 'web'` WM surface alongside terminals, editors, and 3D agent worlds — with 3-bit Wake Leases (`AUDIO` / `NET_PUSH` / `COMPUTE`), automatic idle suspend/resume, and zero-copy `2D <-> 3D` re-binding.
- **Actor-Bound Streams:** Bind avatars, live display surfaces, and bi-directional dialogue streams to any human or agent `actor_id`.

## Quick Start
```bash
cc -std=c11 -O2 -Wall -Wextra -Werror -DSECTIUM_DISPLAY_STATIC -Iinclude src/sectium_display_test.c src/sectium_display.c -o test_display && ./test_display
node --test wm/scriptable-wm.test.mts wm/webframe-provider.test.mts
```

## License
MIT

## Quick Install & Run (Windows / Linux / macOS)

```sh
# Install / hydrate from v11 CAS via holo-pm
holo-pm install repository://github.com/awesome-os/sectium-display@main

# Or project & run directly via holo-project (0-byte hardlink)
holo-project "project://cpp/sectium-display?from=repository://github.com/awesome-os/sectium-display@main"
```
