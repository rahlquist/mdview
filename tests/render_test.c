/* render_test.c — verify the Markdown -> HTML conversion. */
#include "../src/render.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect(const char *name, const char *haystack, const char *needle)
{
    if (!strstr(haystack, needle)) {
        printf("FAIL %-22s: missing '%s'\n", name, needle);
        failures++;
    }
}

int main(void)
{
    const char *md =
        "# Title\n"
        "\n"
        "| a | b |\n"
        "|---|---|\n"
        "| 1 | 2 |\n"
        "\n"
        "- [x] done\n"
        "- [ ] todo\n"
        "\n"
        "```c\nint x = 1;\n```\n"
        "\n"
        "> quoted\n"
        "\n"
        "~~struck~~ and **bold** and `code`\n"
        "\n"
        "Text with a footnote[^1].\n"
        "\n"
        "[^1]: the note\n";

    char *html = mdview_render_html(md, strlen(md), "dark", "Test & <Title>");

    expect("doctype", html, "<!DOCTYPE html>");
    expect("dark-theme", html, "data-theme=\"dark\"");
    expect("title-escaped", html, "<title>Test &amp; &lt;Title&gt;</title>");
    expect("h1", html, "<h1>Title</h1>");
    expect("table", html, "<table>");
    expect("table-cell", html, "<td>1</td>");
    expect("task-list", html, "checkbox");
    expect("code-block", html, "<pre><code class=\"language-c\">");
    expect("blockquote", html, "<blockquote>");
    expect("strikethrough", html, "<del>struck</del>");
    expect("strong", html, "<strong>bold</strong>");
    expect("inline-code", html, "<code>code</code>");
    expect("footnote-ref", html, "fnref-1-1");
    expect("footnote-def", html, "footnotes");
    expect("css", html, "--bg:#1f2125");
    expect("css-footnote", html, ".footnotes{margin-top:2.6em");

    g_free(html);

    char *light = mdview_render_html("# hi\n", 4, NULL, "t");
    expect("light-theme", light, "data-theme=\"light\"");
    expect("light-bg", light, "--bg:#ffffff");
    g_free(light);

    char *welcome = mdview_welcome_html("light");
    expect("welcome", welcome, "Markdown Viewer");
    g_free(welcome);

    if (failures == 0)
        printf("render_test: all checks passed\n");
    else
        printf("render_test: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
