/* render.h — Markdown -> standalone HTML for mdview.
 *
 * This module deliberately has no GTK/WebKit dependency so the rendering
 * pipeline can be exercised headlessly (`mdview --render in.md out.html`).
 */
#ifndef MDVIEW_RENDER_H
#define MDVIEW_RENDER_H

#include <stddef.h>

typedef struct {
    int   level;   /* 1..6 */
    char *text;    /* plain text (markdown syntax stripped) */
} MdviewHeading;

typedef struct {
    MdviewHeading *items;
    size_t         count;
} MdviewOutline;

/* Render markdown to a complete HTML document.  Caller frees the result.
 * theme: "light", "dark" or NULL (light).  title: used for <title>. */
char *mdview_render_html(const char *md, size_t len, const char *theme,
                         const char *title);

/* HTML for the empty-state page shown when no file is open. */
char *mdview_welcome_html(const char *theme);

/* Extract headings (ATX style, fenced code blocks ignored) for the outline
 * sidebar.  Free with mdview_outline_free(). */
MdviewOutline mdview_outline_extract(const char *md, size_t len);
void mdview_outline_free(MdviewOutline *o);

/* The document stylesheet (embedded in every page). */
const char *mdview_css(void);

#endif /* MDVIEW_RENDER_H */
