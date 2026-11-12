# mdview

A standalone, distraction-free **Markdown viewer** for Linux.

mdview renders Markdown files for reading. It has no editor: the web view is
created non-editable, and the application exposes no way to modify a document.

- Repository: <https://github.com/rahlquist/mdview>
- License: MIT (see [LICENSE](LICENSE))

## What it is — and what it is not

| It is | It is not |
| --- | --- |
| A reader for Markdown files | An editor — there is no save, no write-back, no text input into the document |
| A way to move between the Markdown files in a folder | A file manager |
| A static HTML exporter (`--render`) | A publishing pipeline or site generator |
| A viewer that opens external links in your browser | A general web browser |

## Features

- Clean typography with **light / dark / system** theme.
- Sidebar with a **file list** for the current folder and a document **outline**.
- GitHub-flavoured Markdown: tables, task lists, strikethrough, footnotes.
- **Relative images** resolve against the Markdown file's own folder.
- **Links between Markdown files** open rendered inside the viewer; a
  same-file `#anchor` link scrolls within the current document.
- **External links** are handed to your default browser.
- Find in page, zoom, fullscreen, drag & drop.
- **Security:** JavaScript embedded in Markdown's raw HTML does not run. Only
  the application's own page scripts run. See [Security](#security).

The mechanism behind each of these, and the design decisions that earn them,
is documented in [docs/OVERVIEW.md](docs/OVERVIEW.md).

## Requirements

mdview is a GTK 4 application built on WebKitGTK 6.0 and md4c.

| Dependency | pkg-config name | Used for |
| --- | --- | --- |
| GTK 4 | `gtk4` | Window, header bar, sidebar, dialogs |
| WebKitGTK 6.0 | `webkitgtk-6.0` | Document rendering |
| md4c | `md4c`, `md4c-html` | Markdown → HTML |
| GLib | `glib-2.0` | Data structures, settings, file I/O |
| A C toolchain | `gcc`/`cc`, `make`, `pkg-config` | Build |

On Arch Linux the packages are `gtk4`, `webkitgtk-6.0`, `md4c`, `gcc`, `make`
and `pkg-config`. On Debian/Ubuntu the WebKitGTK 6.0 development package is
`libwebkitgtk-6.0-dev`; on Fedora it is `webkitgtk6.0-devel`.

> The build fails immediately with a message naming the missing pkg-config
> packages if `gtk4` or `webkitgtk-6.0` cannot be found.

## Build

```sh
make            # builds ./mdview
make check      # builds and runs the test suite
```

`make check` compiles `tests/outline_test` and `tests/render_test` against
`src/render.c` and runs both. It needs no display.

There is also an optional probe target:

```sh
make probe      # builds tests/js_markup_probe (needs a display to run)
```

`make` honours the usual variables: `PREFIX` (default `$(HOME)/.local`), `CC`
(default `gcc`), and `CFLAGS` (default `-O2 -g -Wall -Wextra
-Wno-unused-parameter`).

## Install

`make install` installs user-locally by default — no root required:

```sh
make install                     # into ~/.local
make install PREFIX=/usr/local   # system-wide (needs write permission)
make uninstall
```

| File | Destination with the default `PREFIX` |
| --- | --- |
| `mdview` | `~/.local/bin/mdview` |
| `mdview.desktop` | `~/.local/share/applications/mdview.desktop` |
| `data/text-markdown.svg` | `~/.local/share/icons/hicolor/scalable/mimetypes/text-markdown.svg` |

After installing, run `update-desktop-database ~/.local/share/applications`
if your desktop environment does not pick the launcher up automatically.

## Usage

```sh
mdview path/to/file.md
```

Or launch **Markdown Viewer** from your application menu and open a file with
`Ctrl+O`.

### Keyboard shortcuts

| Action | Shortcut |
| --- | --- |
| Open… | `Ctrl+O` |
| Find in page… | `Ctrl+F` |
| Find next / previous | Enter in the find bar / the ↑ ↓ buttons |
| Close find bar | `Esc` |
| Toggle sidebar | `Ctrl+B` |
| Zoom in | `Ctrl++` or `Ctrl+=` |
| Zoom out | `Ctrl+-` |
| Reset zoom | `Ctrl+0` |
| Fullscreen | `F11` |
| Quit | `Ctrl+Q` |

### Menu

The header-bar menu button (an `open-menu-symbolic` icon, tooltip “Menu”)
contains: **Open…**, **Find in Page…**, a **View** submenu (**Sidebar**,
**Fullscreen**, **Theme → System / Light / Dark**), **About Markdown Viewer**
and **Quit**.

### Opening files

- Drag a file onto the window — the window accepts a dropped file path or
  `file://` URI.
- Click a Markdown file in the sidebar file list. The list shows the current
  folder's Markdown files, sorted by name, plus a `..` row for the parent
  folder.
- Click an entry in the outline to scroll the document to that heading.
- Follow a link to another Markdown file to open it rendered in the viewer.

### Headless rendering

`--render` writes a complete, self-contained HTML document and exits. It needs
no display, which makes it usable for static export and for scripted checks:

```sh
mdview --render input.md output.html
```

The theme used is the one currently configured (see
[Settings](#settings)); `system` and any unrecognised value produce the light
stylesheet.

Other command-line flags:

```sh
mdview --version     # prints "mdview 1.0"
mdview --help        # prints usage
```

`--render` requires both an input and an output path; with fewer than two
further arguments the option is not taken and the GUI path runs instead.

## Settings

mdview stores its state in `~/.config/mdview/settings.ini`, group
`[General]`. Values are written on quit, on zoom, on theme change, on
fullscreen toggle and when the window closes; the directory is created with
mode `0700` if missing. Toggling the sidebar is not itself a save event.

| Key | Default | Accepted range / values | Effect |
| --- | --- | --- | --- |
| `theme` | `system` | `system`, `light`, `dark` | Colour scheme. `system` follows the OS preference. |
| `zoom` | `1.0` | `0.3` – `3.0` | Page zoom level. Zoom keys clamp to `0.5` – `3.0`. |
| `width` | `1280` | `> 320` and `< 8000` | Window width in pixels. |
| `height` | `820` | `> 240` and `< 8000` | Window height in pixels. |
| `sidebar-pos` | `260` | `> 120` and `< 2000` | Sidebar divider position in pixels. |
| `last-dir` | *(unset)* | any existing directory | Folder shown in the sidebar at startup. A value that is not an existing directory is ignored. |

Out-of-range values are silently discarded and the default is kept. The
sidebar is visible at startup.

## Testing

```sh
make check
```

runs the unit suite:

- `tests/outline_test` — heading extraction: fenced code (backtick and tilde),
  indented code, inline formatting, links/images, trailing `#` handling,
  backslash escapes, setext headings, and empty input. 9 cases.
- `tests/render_test` — HTML output: doctype, theme attribute, title escaping,
  headings, tables, task-list checkboxes, code blocks, blockquotes,
  strikethrough, strong, inline code, footnote reference/definition, and the
  embedded stylesheet.

Expected output:

```
outline_test: all 9 cases passed
render_test: all checks passed
```

`make probe` builds `tests/js_markup_probe`, which loads
`tests/js_markup_probe.html` in a WebKitWebView configured exactly as mdview
configures its own view, and prints `SCRIPT-BLOCKED` when the page's inline
`<script>` elements did not run. It needs a display — on a headless machine,
run it under GTK's broadway backend:

```sh
make probe
gtk4-broadwayd :5 --port 8055 &
GDK_BACKEND=broadway BROADWAY_DISPLAY=:5 tests/js_markup_probe tests/js_markup_probe.html
# → SCRIPT-BLOCKED
```

The GUI rendering path was verified under that same broadway backend: the real
renderer and the real WebKit settings block were driven through a document load,
and a relative image resolved and loaded while the document's own `<script>` did
not run. See
[docs/OVERVIEW.md#verified-claims](docs/OVERVIEW.md#verified-claims).

## Security

mdview's threat model is "read documents from disk", so the document is treated
as untrusted content and the application's own scripts as trusted.

- **JavaScript markup is disabled.** The web view sets
  `enable-javascript-markup` to `false`, so `<script>` elements in Markdown's
  raw HTML do not execute. `enable-javascript` remains `true` because the
  application itself evaluates a small script on every page load to set the
  colour theme and to scroll the outline into view.
- **Raw HTML still reaches the page.** Disabling JavaScript markup does not
  sanitise HTML; md4c passes raw HTML blocks and inline HTML through. Images,
  `onerror`-style attributes and other active-looking markup are present in the
  DOM but inert because the scripts do not run.
- **File access is allowed for `file://` URLs**, which is what makes relative
  images and local links work.
- **External links are delegated**, not navigated: the policy handler hands
  non-`file:`, non-`about:` and non-`data:` URLs to the system's default
  handler via `GtkUriLauncher`, and ignores the in-view navigation.
- **The view is non-editable** and the application has no save path.

`tests/js_markup_probe` exists so this configuration can be re-verified
against a WebKitGTK upgrade rather than trusted from the source alone.

## Known limitations

- **Outline covers ATX headings only.** Setext headings (`Title` / `=====`)
  render as headings but do not appear in the outline. This is covered by the
  `setext-skipped` test case.
- **Code blocks are not syntax-highlighted.** They are styled in a monospace
  block with the language class preserved (for example
  `<code class="language-c">`), but no highlighting is applied.
- **No editor, no save.** This is deliberate.
- **Single document at a time.** The sidebar file list is a browser, not a
  tab bar; opening another file replaces the current document.
- **The GUI needs a display.** `--render` and `make check` are headless; the
  windowed application is not.
- **Arch Linux is the primary target** for the packaged dependency names; other
  distributions need the equivalent GTK 4 / WebKitGTK 6.0 / md4c packages.

## Contributing

- Build and test before opening a change:

  ```sh
  make
  make check
  ```

- **Keep the renderer GUI-free.** `src/render.c` must not acquire GTK or
  WebKit dependencies. That separation is what lets `mdview --render` and
  `make check` run without a display. Put UI code in `src/main.c`.
- **Keep the public rendering behaviour covered.** If you change what
  `mdview_render_html` or `mdview_outline_extract` emits, extend
  `tests/render_test.c` or `tests/outline_test.c` so the change is pinned by a
  test rather than only by inspection.
- **Keep the security configuration verifiable.** If you touch the WebKit
  settings block in `src/main.c`, rebuild the probe (`make probe`) and confirm
  it still reports `SCRIPT-BLOCKED`.
- Documentation for this repository lives in [README.md](README.md) and
  [docs/](docs/); update the affected page in the same change as the behaviour
  it describes.

## Source layout

```
src/render.c   Markdown → HTML, themes, outline extraction (no GUI deps)
src/render.h   renderer interface
src/main.c     GTK application: window, sidebar, web view, actions
tests/         outline_test, render_test, js_markup_probe
data/          text-markdown.svg icon
```

## License

MIT — see [LICENSE](LICENSE).
