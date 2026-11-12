/* outline_test.c — verify heading extraction for the sidebar outline. */
#include "../src/render.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(const char *name, const char *md,
                  const char **expect, size_t n)
{
    MdviewOutline o = mdview_outline_extract(md, strlen(md));

    if (o.count != n) {
        printf("FAIL %-14s: got %zu headings, want %zu\n", name, o.count, n);
        failures++;
    } else {
        for (size_t i = 0; i < n; i++) {
            if (strcmp(o.items[i].text, expect[i]) != 0) {
                printf("FAIL %-14s[%zu]: got '%s', want '%s'\n",
                       name, i, o.items[i].text, expect[i]);
                failures++;
            }
        }
    }
    mdview_outline_free(&o);
}

int main(void)
{
    {
        const char *md = "# One\n\n## Two\n\n```\n# not a heading\n```\n\n### Three\n";
        const char *e[] = { "One", "Two", "Three" };
        check("fenced-code", md, e, 3);
    }
    {
        const char *md = "~~~\n## hidden\n~~~\n# shown\n";
        const char *e[] = { "shown" };
        check("tilde-fence", md, e, 1);
    }
    {
        const char *md = "    # indented code\n# real\n";
        const char *e[] = { "real" };
        check("indented", md, e, 1);
    }
    {
        const char *md = "# Title #\n\n## **Bold** and `code`\n\n### a & b\n";
        const char *e[] = { "Title", "Bold and code", "a & b" };
        check("formatting", md, e, 3);
    }
    {
        const char *md = "# See [docs](https://x) and ![alt](img.png)\n";
        const char *e[] = { "See docs and alt" };
        check("links-images", md, e, 1);
    }
    {
        const char *md = "# C# and snake_case\n";
        const char *e[] = { "C# and snake_case" };
        check("hashes", md, e, 1);
    }
    {
        const char *md = "# a\\*b\n";
        const char *e[] = { "a*b" };
        check("escape", md, e, 1);
    }
    {
        const char *md = "Setext\n======\n\n# atx\n";
        const char *e[] = { "atx" };
        check("setext-skipped", md, e, 1);
    }
    {
        const char *md = "";
        check("empty", md, NULL, 0);
    }

    if (failures == 0)
        printf("outline_test: all 9 cases passed\n");
    else
        printf("outline_test: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
