/* render.c — Markdown -> standalone HTML for mdview.
 *
 * Pure C (GLib + md4c only): no GTK, no WebKit.  The GTK application and the
 * headless `--render` CLI both build their pages through this module.
 */
#include "render.h"

#include <glib.h>
#include <md4c.h>
#include <md4c-html.h>

#include <stdlib.h>
#include <string.h>

/* md4c parser extensions: GitHub-flavoured tables, task lists, strikethrough
 * and footnotes. */
#define MD_PARSER_FLAGS  (MD_FLAG_TABLES | MD_FLAG_TASKLISTS | \
                          MD_FLAG_STRIKETHROUGH | MD_FLAG_FOOTNOTES)
#define MD_RENDER_FLAGS  (MD_HTML_FLAG_SKIP_UTF8_BOM)

static const char MDVIEW_CSS[] =
  ":root{"
  "--bg:#ffffff;--fg:#24292f;--muted:#59636e;--link:#0969da;--accent:#0969da;"
  "--border:#d8dee4;--code-bg:#f6f8fa;--quote-bg:#f6f8fa;--zebra:#fafbfc;"
  "--selection:rgba(9,105,218,.28);"
  "--font:system-ui,-apple-system,\"Segoe UI\",Roboto,Cantarell,\"Noto Sans\","
       "\"Helvetica Neue\",Arial,sans-serif;"
  "--mono:ui-monospace,\"SFMono-Regular\",\"SF Mono\",\"Cascadia Code\","
       "\"JetBrains Mono\",Menlo,Consolas,\"Liberation Mono\",monospace;"
  "}\n"
  "html[data-theme=\"dark\"]{"
  "--bg:#1f2125;--fg:#d6deeb;--muted:#8b939e;--link:#7aa2f7;--accent:#7aa2f7;"
  "--border:#33373d;--code-bg:#26282e;--quote-bg:#26282e;--zebra:#24262b;"
  "--selection:rgba(122,162,247,.30);"
  "}\n"
  "*{box-sizing:border-box}\n"
  "html{background:var(--bg);-webkit-text-size-adjust:100%}\n"
  "body{margin:0;background:var(--bg);color:var(--fg);font-family:var(--font);"
  "font-size:16px;line-height:1.75;-webkit-font-smoothing:antialiased;"
  "text-rendering:optimizeLegibility}\n"
  ".content{max-width:880px;margin:0 auto;padding:3.5rem 2.5rem 7rem}\n"
  "h1,h2,h3,h4,h5,h6{line-height:1.3;font-weight:650;margin:1.7em 0 .6em}\n"
  "h1{font-size:2.1em;margin-top:.4em;padding-bottom:.3em;"
  "border-bottom:1px solid var(--border)}\n"
  "h2{font-size:1.55em;padding-bottom:.25em;border-bottom:1px solid var(--border)}\n"
  "h3{font-size:1.28em}\n"
  "h4{font-size:1.08em}\n"
  "h5,h6{font-size:1em;color:var(--muted)}\n"
  "p{margin:0 0 1.15em}\n"
  "a{color:var(--link);text-decoration:none}\n"
  "a:hover{text-decoration:underline}\n"
  "strong{font-weight:650}\n"
  "hr{border:0;border-top:1px solid var(--border);margin:2.4em 0}\n"
  "ul,ol{margin:0 0 1.15em;padding-left:1.7em}\n"
  "li{margin:.3em 0}\n"
  "li>ul,li>ol{margin:.3em 0}\n"
  "blockquote{margin:0 0 1.15em;padding:.55em 1.1em;border-left:4px solid var(--accent);"
  "background:var(--quote-bg);color:var(--muted);border-radius:0 8px 8px 0}\n"
  "blockquote p:last-child{margin-bottom:0}\n"
  "code{font-family:var(--mono);font-size:.87em;background:var(--code-bg);"
  "border:1px solid var(--border);border-radius:5px;padding:.14em .4em}\n"
  "pre{background:var(--code-bg);border:1px solid var(--border);border-radius:9px;"
  "padding:1em 1.15em;margin:0 0 1.25em;overflow-x:auto;line-height:1.6}\n"
  "pre code{background:none;border:0;padding:0;font-size:.85em;border-radius:0}\n"
  "table{border-collapse:collapse;margin:0 0 1.25em;font-size:.95em}\n"
  "th,td{border:1px solid var(--border);padding:.5em .85em;text-align:left;"
  "vertical-align:top}\n"
  "th{background:var(--code-bg);font-weight:650}\n"
  "tbody tr:nth-child(even) td{background:var(--zebra)}\n"
  "img{max-width:100%;height:auto;border-radius:6px}\n"
  "input[type=\"checkbox\"]{margin-right:.55em;accent-color:var(--accent);"
  "transform:scale(1.05)}\n"
  "li:has(>input[type=\"checkbox\"]){list-style:none;margin-left:-1.5em}\n"
  ".task-list-item{list-style:none;margin-left:-1.5em}\n"
  ".footnotes{margin-top:2.6em;padding-top:1.1em;border-top:1px solid var(--border);"
  "font-size:.9em;color:var(--muted)}\n"
  ".footnotes ol{padding-left:1.4em}\n"
  ".footnotes li{margin:.4em 0}\n"
  "sup a{font-size:.75em;text-decoration:none}\n"
  "::-webkit-scrollbar{width:11px;height:11px}\n"
  "::-webkit-scrollbar-thumb{background:var(--border);border-radius:6px;"
  "border:2px solid var(--bg)}\n"
  "::-webkit-scrollbar-track{background:transparent}\n"
  "::selection{background:var(--selection)}\n"
  ".placeholder{text-align:center;padding-top:22vh;color:var(--muted)}\n"
  ".placeholder h1{border:0;color:var(--fg)}\n"
  ".placeholder .keys{display:inline-block;margin-top:1.2em;font-size:.9em;"
  "background:var(--code-bg);border:1px solid var(--border);border-radius:8px;"
  "padding:.6em 1.1em}\n";

const char *mdview_css(void)
{
    return MDVIEW_CSS;
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void html_append_escaped(GString *s, const char *text)
{
    for (const char *p = text ? text : ""; *p; p++) {
        switch (*p) {
        case '&': g_string_append(s, "&amp;");  break;
        case '<': g_string_append(s, "&lt;");   break;
        case '>': g_string_append(s, "&gt;");   break;
        case '"': g_string_append(s, "&quot;"); break;
        default:  g_string_append_c(s, *p);     break;
        }
    }
}

static void md_out_cb(const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    g_string_append_len((GString *)userdata, text, size);
}

static const char *theme_attr(const char *theme)
{
    return (theme && strcmp(theme, "dark") == 0) ? "dark" : "light";
}

static char *build_document(const char *theme, const char *title, const char *body)
{
    GString *doc = g_string_sized_new(strlen(body) + 4096);

    g_string_append(doc, "<!DOCTYPE html>\n<html lang=\"en\" data-theme=\"");
    g_string_append(doc, theme_attr(theme));
    g_string_append(doc, "\">\n<head>\n<meta charset=\"utf-8\">\n");
    g_string_append(doc, "<meta name=\"viewport\" content=\"width=device-width, "
                         "initial-scale=1\">\n<title>");
    html_append_escaped(doc, title);
    g_string_append(doc, "</title>\n<style>\n");
    g_string_append(doc, MDVIEW_CSS);
    g_string_append(doc, "\n</style>\n</head>\n<body>\n");
    g_string_append(doc, body);
    g_string_append(doc, "\n</body>\n</html>\n");

    return g_string_free(doc, FALSE);
}

char *mdview_render_html(const char *md, size_t len, const char *theme,
                         const char *title)
{
    GString *body = g_string_sized_new((gsize)len + 256);
    md_html(md, (MD_SIZE)len, md_out_cb, body, MD_PARSER_FLAGS, MD_RENDER_FLAGS);

    char *doc = build_document(theme, title, body->str);
    g_string_free(body, TRUE);
    return doc;
}

char *mdview_welcome_html(const char *theme)
{
    static const char WELCOME[] =
        "<main class=\"content placeholder\">\n"
        "<h1>Markdown Viewer</h1>\n"
        "<p>Open a Markdown file to start reading.</p>\n"
        "<p class=\"keys\">Ctrl+O &mdash; open &nbsp;&middot;&nbsp; "
        "drag &amp; drop a file anywhere</p>\n"
        "</main>\n";

    return build_document(theme, "Markdown Viewer", WELCOME);
}

/* ------------------------------------------------------------------ */
/* outline extraction                                                  */
/* ------------------------------------------------------------------ */

/* Strip markdown inline syntax so the result matches the rendered text. */
static char *plain_heading(const char *raw, size_t n)
{
    GString *o = g_string_sized_new(n + 1);
    size_t i = 0;

    while (i < n) {
        char c = raw[i];

        if (c == '\\' && i + 1 < n) {                 /* backslash escape */
            g_string_append_c(o, raw[i + 1]);
            i += 2;
            continue;
        }
        if (c == '!' && i + 1 < n && raw[i + 1] == '[') {   /* image -> alt */
            size_t j = i + 2, depth = 1;
            while (j < n && depth) {
                if (raw[j] == '[') depth++;
                else if (raw[j] == ']') depth--;
                j++;
            }
            if (depth == 0) {
                g_string_append_len(o, raw + i + 2, j - i - 3);
                if (j < n && raw[j] == '(') {
                    while (j < n && raw[j] != ')') j++;
                    if (j < n) j++;
                }
                i = j;
                continue;
            }
        }
        if (c == '[') {                               /* link -> text */
            size_t j = i + 1, depth = 1;
            while (j < n && depth) {
                if (raw[j] == '[') depth++;
                else if (raw[j] == ']') depth--;
                j++;
            }
            if (depth == 0) {
                g_string_append_len(o, raw + i + 1, j - i - 2);
                if (j < n && raw[j] == '(') {
                    while (j < n && raw[j] != ')') j++;
                    if (j < n) j++;
                } else if (j < n && raw[j] == '[') {
                    while (j < n && raw[j] != ']') j++;
                    if (j < n) j++;
                }
                i = j;
                continue;
            }
        }
        if (c == '<') {                               /* inline HTML tag */
            size_t j = i + 1;
            while (j < n && raw[j] != '>') j++;
            if (j < n) { i = j + 1; continue; }
        }
        if (c == '*') { i++; continue; }              /* emphasis marker */
        if (c == '`') { i++; continue; }              /* code marker */
        if (c == '~' && i + 1 < n && raw[i + 1] == '~') { i += 2; continue; }
        if (c == '_' && i + 1 < n && raw[i + 1] == '_') { i += 2; continue; }

        g_string_append_c(o, c);
        i++;
    }

    /* collapse whitespace and trim */
    GString *r = g_string_sized_new(o->len + 1);
    gboolean pending_space = FALSE;
    for (gsize k = 0; k < o->len; k++) {
        char ch = o->str[k];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            pending_space = TRUE;
        } else {
            if (pending_space && r->len > 0) g_string_append_c(r, ' ');
            pending_space = FALSE;
            g_string_append_c(r, ch);
        }
    }
    g_string_free(o, TRUE);
    return g_string_free(r, FALSE);
}

MdviewOutline mdview_outline_extract(const char *md, size_t len)
{
    MdviewOutline o = { NULL, 0 };
    size_t cap = 0;
    size_t i = 0;
    char fence = 0;
    size_t fence_len = 0;

    while (i < len) {
        size_t j = i;
        while (j < len && md[j] != '\n') j++;
        size_t llen = j - i;
        if (llen && md[i + llen - 1] == '\r') llen--;

        const char *line = md + i;
        size_t k = 0;
        while (k < llen && (line[k] == ' ' || line[k] == '\t')) k++;

        if (fence) {
            /* closing fence: same char, at least as long, nothing else */
            size_t m = k, cnt = 0;
            while (m < llen && line[m] == fence) { cnt++; m++; }
            if (cnt >= fence_len) {
                size_t q = m;
                while (q < llen && (line[q] == ' ' || line[q] == '\t')) q++;
                if (q == llen) fence = 0;
            }
        } else if (k + 3 <= llen && (line[k] == '`' || line[k] == '~')) {
            char c = line[k];
            size_t m = k;
            size_t cnt = 0;
            while (m < llen && line[m] == c) { cnt++; m++; }
            if (cnt >= 3) {
                /* a backtick fence may not contain a backtick on the line */
                if (c == '~' || !memchr(line + m, '`', llen - m)) {
                    fence = c;
                    fence_len = cnt;
                }
            }
        }

        /* ATX heading: at most three leading spaces (more means code block) */
        if (!fence && k <= 3) {
            size_t h = k;
            int level = 0;
            while (h < llen && line[h] == '#' && level < 6) { level++; h++; }

            if (level >= 1 && (h == llen || line[h] == ' ' || line[h] == '\t')) {
                while (h < llen && (line[h] == ' ' || line[h] == '\t')) h++;

                size_t end = llen;
                size_t e = end;
                while (e > h && line[e - 1] == '#') e--;
                if (e < end && (e == h || line[e - 1] == ' ' || line[e - 1] == '\t'))
                    end = e;
                while (end > h && (line[end - 1] == ' ' || line[end - 1] == '\t'))
                    end--;

                if (end > h) {
                    char *text = plain_heading(line + h, end - h);
                    if (text[0] != '\0') {
                        if (o.count == cap) {
                            cap = cap ? cap * 2 : 8;
                            o.items = g_realloc(o.items, cap * sizeof(MdviewHeading));
                        }
                        o.items[o.count].level = level;
                        o.items[o.count].text = text;
                        o.count++;
                    } else {
                        g_free(text);
                    }
                }
            }
        }

        i = (j < len) ? j + 1 : j;
    }

    return o;
}

void mdview_outline_free(MdviewOutline *o)
{
    if (!o) return;
    for (size_t i = 0; i < o->count; i++)
        g_free(o->items[i].text);
    g_free(o->items);
    o->items = NULL;
    o->count = 0;
}
