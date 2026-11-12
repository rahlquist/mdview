# mdview

A standalone, distraction-free **Markdown viewer** for Linux (GTK 4 + WebKitGTK).
It renders Markdown files for reading — there is no editor.

Inspired by the reading experience of Ghostwriter, but built from scratch:
mdview is a clean-room implementation (no Ghostwriter code) and is MIT
licensed.

## Features

- Clean typography with light / dark / system theme
- Sidebar with a file browser (current folder) and a document outline
- GitHub-flavoured Markdown: tables, task lists, strikethrough, footnotes
- Relative images resolve against the Markdown file's folder
- Links between Markdown files open rendered in the viewer
- External links open in your default browser
- Find in page, zoom, fullscreen (F11), drag & drop
- Security: JavaScript in Markdown's raw HTML is disabled; only the
  application's own page scripts run

## Build

Dependencies (Arch): `gtk4`, `webkit2gtk-4.1`, `md4c`

```sh
make
make check          # run the test suite (outline + renderer)
```

## Install (user-local, no root)

```sh
make install        # installs into ~/.local
```

This places the binary in `~/.local/bin/mdview`, a `.desktop` launcher in
`~/.local/share/applications/`, and an icon in the hicolor theme.

## Usage

```sh
mdview path/to/file.md
```

Or launch **Markdown Viewer** from your application menu and open a file with
`Ctrl+O`.

| Action         | Shortcut         |
| -------------- | ---------------- |
| Open           | `Ctrl+O`         |
| Find in page   | `Ctrl+F`         |
| Close find     | `Esc`            |
| Toggle sidebar | `Ctrl+B`         |
| Zoom           | `Ctrl + / - / 0` |
| Fullscreen     | `F11`            |
| Quit           | `Ctrl+Q`         |

## Headless rendering

The renderer is a standalone module, so it can be exercised without a GUI
(used by the test suite to verify output):

```sh
mdview --render input.md output.html
```

## Testing

```sh
make check     # unit tests: heading extraction + HTML rendering
make probe     # build the WebKit security-configuration probe
```

The GUI itself was verified headlessly with GTK's broadway backend
(`gtk4-broadwayd`) and a browser, plus pixel-level checks of the rendered
output.

## Known limitations

- Setext-style headings (`Title` / `=====`) are rendered but not listed in the
  outline (ATX headings only).
- Code blocks are styled but not syntax-highlighted.

## Source layout

```
src/render.c   Markdown -> HTML, themes, outline extraction (no GUI deps)
src/main.c     GTK application: window, sidebar, web view, actions
tests/         outline_test, render_test, js_markup_probe
```
