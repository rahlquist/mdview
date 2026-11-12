# mdview test document

A comprehensive sample used to verify the renderer. It exercises headings,
tables, code, task lists, quotes, footnotes, images and links.

## Typography

Text with **bold**, *italic*, ~~strikethrough~~, `inline code`, and a
[link to example.com](https://example.com).

> A blockquote with a longer sentence so the left border and padding are
> clearly visible in the screenshot.

## Code

```c
/* fenced code block */
#include <stdio.h>

int main(void) {
    printf("hello, mdview\n");
    return 0;
}
```

~~~python
# a tilde fence
def greet(name):
    print(f"hi {name}")
~~~

    # this indented line is code, not a heading

## Tables

| Feature        | Supported | Notes                    |
| -------------- | --------- | ------------------------ |
| Tables         | yes       | GFM style                |
| Task lists     | yes       | clickable checkboxes     |
| Footnotes      | yes       | `[^1]` references        |
| Strikethrough  | yes       | `~~text~~`               |

## Task list

- [x] Render Markdown
- [x] Add light and dark themes
- [ ] Add syntax highlighting

## Outline fence test

```
# this line is inside a fence and must NOT appear in the outline
## neither should this one
```

## Images and links

Relative image (resolved against this file's folder):

![mdview test image](img/test.svg)

A link to [another Markdown file](other.md) renders that file in the viewer,
and an [internal anchor](#typography) scrolls within this one.

## Footnotes

Here is a footnote reference[^1] and another one[^note].

[^1]: The first footnote text.
[^note]: A named footnote with more explanation.

---

## Setext heading

Title case text followed by an equals line is a level 1 heading, and a
dashes line is level 2. Both should render and scroll correctly.

### Deep nesting

#### Level four

##### Level five

###### Level six
