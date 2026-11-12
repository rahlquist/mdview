/* main.c — mdview: a standalone Markdown viewer.
 *
 * GTK 4 + WebKitGTK shell around the renderer in render.c.  This is a viewer
 * only: the web view is explicitly non-editable and renders documents.
 *
 * Features
 *   - Markdown rendered by md4c, styled like a distraction-free editor
 *   - Light / dark / system theme
 *   - Sidebar with a file browser and a document outline
 *   - Relative images and inter-linked Markdown files resolved on disk
 *   - External links handed to the default browser
 *   - Find in page, zoom, fullscreen, drag & drop
 */
#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include <stdlib.h>
#include <string.h>

#include "render.h"

#define APP_ID      "io.github.rahlquist.mdview"
#define APP_VERSION "1.0"
#define GROUP_NAME  "General"

static const char *MD_EXTS[] = {
    "md", "markdown", "mdown", "mkd", "mkdn", "mdwn", "mkdown", NULL
};

typedef struct {
    GtkApplication  *gapp;
    GtkWindow       *window;
    WebKitWebView   *webview;
    GtkHeaderBar    *header;
    GtkPaned        *paned;
    GtkWidget       *sidebar;
    GtkListBox      *file_list;
    GtkListBox      *outline_list;
    GtkLabel        *title_label;
    GtkLabel        *subtitle_label;
    GtkWidget       *find_bar;
    GtkWidget       *find_entry;
    GtkToggleButton *sidebar_btn;

    GFile         *file;
    GFile         *dir;
    GSimpleAction *theme_action;
    GSimpleAction *sidebar_action;

    char    *theme;             /* "system" | "light" | "dark" */
    double   zoom;
    int      width, height, sidebar_pos;
    char    *last_dir;
    gboolean sidebar_visible;
    gboolean fullscreen;
} App;

/* forward declarations — UI callbacks defined further down */
static void refresh_file_list(App *a);
static GMenu *build_menu(App *a);
static void set_sidebar_visible(App *a, gboolean v);
static void set_fullscreen(App *a, gboolean on);

static void action_open_cb(GSimpleAction *action, GVariant *param, gpointer user_data);
static void action_find_cb(GSimpleAction *action, GVariant *param, gpointer user_data);
static void action_escape_cb(GSimpleAction *action, GVariant *param, gpointer user_data);
static void action_fullscreen_cb(GSimpleAction *action, GVariant *param, gpointer user_data);
static void action_zoomin_cb(GSimpleAction *action, GVariant *param, gpointer user_data);
static void action_zoomout_cb(GSimpleAction *action, GVariant *param, gpointer user_data);
static void action_zoomreset_cb(GSimpleAction *action, GVariant *param, gpointer user_data);
static void action_sidebar_cb(GSimpleAction *action, GVariant *value, gpointer user_data);
static void action_theme_cb(GSimpleAction *action, GVariant *value, gpointer user_data);
static void action_about_cb(GSimpleAction *action, GVariant *param, gpointer user_data);
static void action_quit_cb(GSimpleAction *action, GVariant *param, gpointer user_data);

static void on_sidebar_btn(GtkToggleButton *btn, App *a);
static void on_find_activate(GtkSearchEntry *entry, App *a);
static void on_find_next(GtkButton *btn, App *a);
static void on_find_previous(GtkButton *btn, App *a);
static void on_find_close(App *a);
static void on_load_changed(WebKitWebView *wv, WebKitLoadEvent event, App *a);
static gboolean on_decide_policy(WebKitWebView *wv, WebKitPolicyDecision *decision,
                                 WebKitPolicyDecisionType decision_type, App *a);
static gboolean on_drop(GtkDropTarget *target, const GValue *value,
                        double x, double y, App *a);
static gboolean on_close_request(GtkWindow *window, App *a);

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static gboolean is_markdown_path(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path) return FALSE;

    char *ext = g_ascii_strdown(dot + 1, -1);
    gboolean ok = FALSE;
    for (int i = 0; MD_EXTS[i]; i++) {
        if (strcmp(ext, MD_EXTS[i]) == 0) { ok = TRUE; break; }
    }
    g_free(ext);
    return ok;
}

static char *prettify_path(const char *path)
{
    if (!path) return NULL;
    const char *home = g_get_home_dir();
    size_t hl = home ? strlen(home) : 0;
    if (hl > 0 && strncmp(path, home, hl) == 0 &&
        (path[hl] == '/' || path[hl] == '\0'))
        return g_strconcat("~", path + hl, NULL);
    return g_strdup(path);
}

/* URI of the directory containing file, with a trailing slash — used as the
 * base URI so relative images resolve against the Markdown file's folder. */
static char *file_dir_uri(GFile *file)
{
    char *uri = NULL;
    GFile *dir = g_file_get_parent(file);
    if (!dir) return NULL;

    char *path = g_file_get_path(dir);
    g_object_unref(dir);
    if (!path) return NULL;

    uri = g_filename_to_uri(path, NULL, NULL);
    g_free(path);
    if (!uri) return NULL;
    if (uri[strlen(uri) - 1] == '/') return uri;

    char *res = g_strconcat(uri, "/", NULL);
    g_free(uri);
    return res;
}

static char *js_escape(const char *s)
{
    GString *o = g_string_new(NULL);
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        switch (*p) {
        case '\\': g_string_append(o, "\\\\"); break;
        case '"':  g_string_append(o, "\\\""); break;
        case '\n': g_string_append(o, "\\n");  break;
        case '\r': g_string_append(o, "\\r");  break;
        case '\t': g_string_append(o, "\\t");  break;
        default:
            if (*p < 0x20) g_string_append_printf(o, "\\u%04x", *p);
            else           g_string_append_c(o, *p);
            break;
        }
    }
    return g_string_free(o, FALSE);
}

static void list_clear(GtkListBox *box)
{
    GtkWidget *row;
    while ((row = gtk_widget_get_first_child(GTK_WIDGET(box))) != NULL)
        gtk_list_box_remove(box, row);
}

/* ------------------------------------------------------------------ */
/* settings                                                            */
/* ------------------------------------------------------------------ */

static char *settings_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "mdview", "settings.ini", NULL);
}

static void load_settings(App *a)
{
    char *path = settings_path();
    GKeyFile *kf = g_key_file_new();

    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        char *theme = g_key_file_get_string(kf, GROUP_NAME, "theme", NULL);
        if (theme) { g_free(a->theme); a->theme = theme; }

        double zoom = g_key_file_get_double(kf, GROUP_NAME, "zoom", NULL);
        if (zoom >= 0.3 && zoom <= 3.0) a->zoom = zoom;

        int w = g_key_file_get_integer(kf, GROUP_NAME, "width", NULL);
        int h = g_key_file_get_integer(kf, GROUP_NAME, "height", NULL);
        if (w > 320 && w < 8000) a->width = w;
        if (h > 240 && h < 8000) a->height = h;

        int pos = g_key_file_get_integer(kf, GROUP_NAME, "sidebar-pos", NULL);
        if (pos > 120 && pos < 2000) a->sidebar_pos = pos;

        char *dir = g_key_file_get_string(kf, GROUP_NAME, "last-dir", NULL);
        if (dir) {
            if (g_file_test(dir, G_FILE_TEST_IS_DIR)) a->last_dir = dir;
            else g_free(dir);
        }
    }

    g_key_file_free(kf);
    g_free(path);
}

static void save_settings(App *a)
{
    if (!a->window) return;

    int w = gtk_widget_get_width(GTK_WIDGET(a->window));
    int h = gtk_widget_get_height(GTK_WIDGET(a->window));
    if (w > 0 && h > 0) { a->width = w; a->height = h; }

    if (a->dir) {
        char *p = g_file_get_path(a->dir);
        if (p) { g_free(a->last_dir); a->last_dir = p; }
    }

    char *path = settings_path();
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    GKeyFile *kf = g_key_file_new();
    g_key_file_set_string(kf, GROUP_NAME, "theme", a->theme);
    g_key_file_set_double(kf, GROUP_NAME, "zoom", a->zoom);
    g_key_file_set_integer(kf, GROUP_NAME, "width", a->width);
    g_key_file_set_integer(kf, GROUP_NAME, "height", a->height);
    g_key_file_set_integer(kf, GROUP_NAME, "sidebar-pos", a->sidebar_pos);
    if (a->last_dir)
        g_key_file_set_string(kf, GROUP_NAME, "last-dir", a->last_dir);

    g_key_file_save_to_file(kf, path, NULL);
    g_key_file_free(kf);
    g_free(path);
}

/* ------------------------------------------------------------------ */
/* WebKit helpers                                                      */
/* ------------------------------------------------------------------ */

static void js_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
    (void)user_data;
    GError *err = NULL;
    JSCValue *v = webkit_web_view_evaluate_javascript_finish(
                     WEBKIT_WEB_VIEW(source), res, &err);
    if (err) g_error_free(err);
    if (v)   g_object_unref(v);
}

static void js_eval(WebKitWebView *wv, const char *script)
{
    webkit_web_view_evaluate_javascript(wv, script, -1, NULL, NULL, NULL,
                                        js_done, NULL);
}

static void apply_theme_js(App *a)
{
    if (!a->webview) return;

    const char *js;
    if (g_strcmp0(a->theme, "dark") == 0)
        js = "document.documentElement.dataset.theme='dark'";
    else if (g_strcmp0(a->theme, "light") == 0)
        js = "document.documentElement.dataset.theme='light'";
    else
        js = "document.documentElement.dataset.theme = "
             "window.matchMedia('(prefers-color-scheme: dark)').matches"
             " ? 'dark' : 'light'";

    js_eval(a->webview, js);
}

/* ------------------------------------------------------------------ */
/* document loading                                                    */
/* ------------------------------------------------------------------ */

static void refresh_outline(App *a, const char *md, size_t len);

static void load_file(App *a, GFile *file)
{
    GError *err = NULL;
    char *data = NULL;
    gsize len = 0;

    if (!g_file_load_contents(file, NULL, &data, &len, NULL, &err)) {
        char *name = g_file_get_basename(file);
        if (a->window) {
            GtkWidget *d = GTK_WIDGET(gtk_alert_dialog_new("Unable to open %s", name));
            gtk_alert_dialog_set_detail(GTK_ALERT_DIALOG(d),
                                        err ? err->message : "unknown error");
            gtk_alert_dialog_show(GTK_ALERT_DIALOG(d), a->window);
        }
        g_free(name);
        if (err) g_error_free(err);
        return;
    }

    char *name = g_file_get_basename(file);
    char *html = mdview_render_html(data, len, a->theme, name);
    char *dir_uri = file_dir_uri(file);

    webkit_web_view_load_html(a->webview, html, dir_uri);

    g_set_object(&a->file, file);
    gtk_label_set_text(a->title_label, name);
    gtk_window_set_title(a->window, name);

    GFile *dir = g_file_get_parent(file);
    char *dirpath = dir ? g_file_get_path(dir) : NULL;
    char *display = prettify_path(dirpath);
    gtk_label_set_text(a->subtitle_label, display ? display : "");
    g_free(display);

    if (dir && (!a->dir || !g_file_equal(a->dir, dir))) {
        g_set_object(&a->dir, dir);
        refresh_file_list(a);
        if (dirpath) { g_free(a->last_dir); a->last_dir = g_strdup(dirpath); }
    }

    refresh_outline(a, data, len);

    g_free(dirpath);
    if (dir) g_object_unref(dir);
    g_free(dir_uri);
    g_free(html);
    g_free(name);
    g_free(data);
}

static void show_welcome(App *a)
{
    char *html = mdview_welcome_html(a->theme);
    webkit_web_view_load_html(a->webview, html, NULL);
    g_free(html);

    gtk_label_set_text(a->title_label, "Markdown Viewer");
    gtk_label_set_text(a->subtitle_label, "");
    gtk_window_set_title(a->window, "Markdown Viewer");
    list_clear(a->outline_list);
}

/* ------------------------------------------------------------------ */
/* sidebar                                                             */
/* ------------------------------------------------------------------ */

static void add_file_row(App *a, GFile *file, const char *label)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *lab = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(lab), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(lab), PANGO_ELLIPSIZE_END);
    gtk_widget_set_margin_start(lab, 12);
    gtk_widget_set_margin_end(lab, 12);
    gtk_widget_set_margin_top(lab, 7);
    gtk_widget_set_margin_bottom(lab, 7);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lab);
    g_object_set_data_full(G_OBJECT(row), "file", g_object_ref(file),
                           g_object_unref);
    gtk_list_box_insert(a->file_list, row, -1);
}

static gint cmp_name(gconstpointer x, gconstpointer y)
{
    return g_strcmp0(*(const char *const *)x, *(const char *const *)y);
}

static void refresh_file_list(App *a)
{
    list_clear(a->file_list);
    if (!a->dir) return;

    GFile *parent = g_file_get_parent(a->dir);
    if (parent) {
        add_file_row(a, parent, "..");
        g_object_unref(parent);
    }

    GError *err = NULL;
    GFileEnumerator *e = g_file_enumerate_children(
        a->dir,
        G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NONE, NULL, &err);
    if (!e) {
        if (err) g_error_free(err);
        return;
    }

    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    GFileInfo *info;
    while ((info = g_file_enumerator_next_file(e, NULL, &err)) != NULL) {
        const char *n = g_file_info_get_name(info);
        if (n && g_file_info_get_file_type(info) != G_FILE_TYPE_DIRECTORY &&
            is_markdown_path(n))
            g_ptr_array_add(names, g_strdup(n));
        g_object_unref(info);
    }
    g_object_unref(e);
    if (err) g_error_free(err);

    g_ptr_array_sort(names, cmp_name);
    for (guint i = 0; i < names->len; i++) {
        const char *n = g_ptr_array_index(names, i);
        GFile *f = g_file_get_child(a->dir, n);
        add_file_row(a, f, n);
        g_object_unref(f);
    }
    g_ptr_array_unref(names);
}

static void refresh_outline(App *a, const char *md, size_t len)
{
    list_clear(a->outline_list);

    MdviewOutline o = mdview_outline_extract(md, len);
    for (size_t i = 0; i < o.count; i++) {
        MdviewHeading *h = &o.items[i];

        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *lab = gtk_label_new(h->text);
        gtk_label_set_xalign(GTK_LABEL(lab), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(lab), PANGO_ELLIPSIZE_END);
        gtk_widget_set_margin_start(lab, 12 + (h->level - 1) * 14);
        gtk_widget_set_margin_end(lab, 12);
        gtk_widget_set_margin_top(lab, 5);
        gtk_widget_set_margin_bottom(lab, 5);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lab);
        g_object_set_data_full(G_OBJECT(row), "text", g_strdup(h->text), g_free);
        gtk_list_box_insert(a->outline_list, row, -1);
    }
    mdview_outline_free(&o);
}

static void outline_scroll_to(App *a, const char *text)
{
    if (!a->webview || !text) return;

    char *esc = js_escape(text);
    char *js = g_strdup_printf(
        "(()=>{const t=%s;const hs=[...document.querySelectorAll"
        "('h1,h2,h3,h4,h5,h6')];const h=hs.find(e=>e.textContent.trim()===t);"
        "if(h){h.scrollIntoView({block:'start'});return '1';}return '0';})()", esc);
    js_eval(a->webview, js);
    g_free(js);
    g_free(esc);
}

static void on_file_row_activated(GtkListBox *box, GtkListBoxRow *row, App *a)
{
    (void)box;
    GFile *f = g_object_get_data(G_OBJECT(row), "file");
    if (f) load_file(a, f);
}

static void on_outline_row_activated(GtkListBox *box, GtkListBoxRow *row, App *a)
{
    (void)box;
    char *text = g_object_get_data(G_OBJECT(row), "text");
    if (text) outline_scroll_to(a, text);
}

/* ------------------------------------------------------------------ */
/* UI construction                                                     */
/* ------------------------------------------------------------------ */

static GtkWidget *build_sidebar(App *a)
{
    GtkWidget *stack = gtk_stack_new();
    /* no transition: keeps page switches reliable on every backend,
     * including broadway's damage tracking */
    gtk_stack_set_transition_type(GTK_STACK(stack),
                                  GTK_STACK_TRANSITION_TYPE_NONE);

    /* --- files --- */
    a->file_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(a->file_list, GTK_SELECTION_NONE);
    gtk_list_box_set_activate_on_single_click(a->file_list, TRUE);
    gtk_list_box_set_show_separators(a->file_list, TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(a->file_list), FALSE);
    g_signal_connect(a->file_list, "row-activated",
                     G_CALLBACK(on_file_row_activated), a);

    GtkWidget *files_sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(files_sw),
                                  GTK_WIDGET(a->file_list));
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(files_sw),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_stack_add_titled(GTK_STACK(stack), files_sw, "files", "Files");

    /* --- outline --- */
    a->outline_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(a->outline_list, GTK_SELECTION_NONE);
    gtk_list_box_set_activate_on_single_click(a->outline_list, TRUE);
    gtk_list_box_set_show_separators(a->outline_list, TRUE);
    g_signal_connect(a->outline_list, "row-activated",
                     G_CALLBACK(on_outline_row_activated), a);

    GtkWidget *outline_sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(outline_sw),
                                  GTK_WIDGET(a->outline_list));
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(outline_sw),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_stack_add_titled(GTK_STACK(stack), outline_sw, "outline", "Outline");

    GtkWidget *switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(box), switcher);
    gtk_box_append(GTK_BOX(box), stack);
    /* the stack must fill the sidebar's height, otherwise it only takes its
     * natural height and the lists are clipped to the first row */
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_widget_set_hexpand(stack, TRUE);
    gtk_widget_set_valign(stack, GTK_ALIGN_FILL);
    gtk_widget_set_size_request(box, 250, -1);

    return box;
}

static GtkWidget *build_find_bar(App *a)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(bar, 10);
    gtk_widget_set_margin_end(bar, 10);
    gtk_widget_set_margin_top(bar, 6);
    gtk_widget_set_margin_bottom(bar, 6);

    GtkWidget *entry = gtk_search_entry_new();
    gtk_widget_set_hexpand(entry, TRUE);
    a->find_entry = entry;

    GtkWidget *prev = gtk_button_new_from_icon_name("go-up-symbolic");
    GtkWidget *next = gtk_button_new_from_icon_name("go-down-symbolic");
    GtkWidget *closeb = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_set_tooltip_text(prev, "Find previous");
    gtk_widget_set_tooltip_text(next, "Find next");
    gtk_widget_set_tooltip_text(closeb, "Close find bar");

    gtk_box_append(GTK_BOX(bar), entry);
    gtk_box_append(GTK_BOX(bar), prev);
    gtk_box_append(GTK_BOX(bar), next);
    gtk_box_append(GTK_BOX(bar), closeb);

    g_signal_connect(entry, "activate", G_CALLBACK(on_find_activate), a);
    g_signal_connect(prev, "clicked", G_CALLBACK(on_find_previous), a);
    g_signal_connect(next, "clicked", G_CALLBACK(on_find_next), a);
    g_signal_connect(closeb, "clicked", G_CALLBACK(on_find_close), a);

    gtk_widget_set_visible(bar, FALSE);
    return bar;
}

static void set_sidebar_visible(App *a, gboolean v)
{
    if (a->fullscreen) v = FALSE;

    a->sidebar_visible = v;
    gtk_widget_set_visible(a->sidebar, v);
    if (a->sidebar_action)
        g_simple_action_set_state(a->sidebar_action, g_variant_new_boolean(v));
    if (a->sidebar_btn) {
        gtk_toggle_button_set_active(a->sidebar_btn, v);
        gtk_button_set_icon_name(GTK_BUTTON(a->sidebar_btn),
                                 v ? "sidebar-hide-symbolic"
                                   : "sidebar-show-symbolic");
    }
}

static void set_fullscreen(App *a, gboolean on)
{
    if (a->fullscreen == on) return;
    a->fullscreen = on;

    if (on) gtk_window_fullscreen(a->window);
    else    gtk_window_unfullscreen(a->window);

    set_sidebar_visible(a, a->sidebar_visible);
    save_settings(a);
}

static void build_ui(App *a)
{
    GtkWidget *win = gtk_application_window_new(a->gapp);
    a->window = GTK_WINDOW(win);
    gtk_window_set_default_size(a->window, a->width, a->height);
    gtk_window_set_title(a->window, "Markdown Viewer");

    /* ---- header bar ---- */
    GtkWidget *hb = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hb), TRUE);
    a->header = GTK_HEADER_BAR(hb);
    gtk_window_set_titlebar(a->window, hb);

    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn),
                                   G_MENU_MODEL(build_menu(a)));
    gtk_widget_set_tooltip_text(menu_btn, "Menu");
    gtk_header_bar_pack_start(a->header, menu_btn);

    GtkWidget *open_btn = gtk_button_new_from_icon_name("document-open-symbolic");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(open_btn), "app.open");
    gtk_widget_set_tooltip_text(open_btn, "Open Markdown file (Ctrl+O)");
    gtk_header_bar_pack_start(a->header, open_btn);

    GtkWidget *side_btn = gtk_toggle_button_new();
    a->sidebar_btn = GTK_TOGGLE_BUTTON(side_btn);
    gtk_button_set_icon_name(GTK_BUTTON(side_btn), "sidebar-hide-symbolic");
    gtk_widget_set_tooltip_text(side_btn, "Toggle sidebar (Ctrl+B)");
    g_signal_connect(side_btn, "clicked", G_CALLBACK(on_sidebar_btn), a);
    gtk_header_bar_pack_start(a->header, side_btn);

    /* centred title */
    GtkWidget *titlebox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    a->title_label = GTK_LABEL(gtk_label_new("Markdown Viewer"));
    a->subtitle_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(a->title_label), "title");
    gtk_widget_add_css_class(GTK_WIDGET(a->subtitle_label), "subtitle");
    gtk_label_set_xalign(a->title_label, 0.5);
    gtk_label_set_xalign(a->subtitle_label, 0.5);
    gtk_label_set_ellipsize(a->title_label, PANGO_ELLIPSIZE_END);
    gtk_label_set_ellipsize(a->subtitle_label, PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(titlebox), GTK_WIDGET(a->title_label));
    gtk_box_append(GTK_BOX(titlebox), GTK_WIDGET(a->subtitle_label));
    gtk_header_bar_set_title_widget(a->header, titlebox);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        ".subtitle { opacity: 0.65; font-size: 11px; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    /* ---- web view ---- */
    WebKitWebView *wv = WEBKIT_WEB_VIEW(webkit_web_view_new());
    a->webview = wv;

    WebKitSettings *s = webkit_settings_new();
    webkit_settings_set_enable_javascript(s, TRUE);
    /* let the app drive the page, but keep markup scripts from running */
    webkit_settings_set_enable_javascript_markup(s, FALSE);
    webkit_settings_set_allow_file_access_from_file_urls(s, TRUE);
    webkit_settings_set_enable_developer_extras(s, FALSE);
    webkit_settings_set_enable_back_forward_navigation_gestures(s, FALSE);
    webkit_web_view_set_settings(wv, s);
    webkit_web_view_set_zoom_level(wv, a->zoom);
    webkit_web_view_set_editable(wv, FALSE);       /* viewer, not an editor */
    g_object_unref(s);

    g_signal_connect(wv, "load-changed", G_CALLBACK(on_load_changed), a);
    g_signal_connect(wv, "decide-policy", G_CALLBACK(on_decide_policy), a);

    /* ---- layout ---- */
    a->sidebar = build_sidebar(a);

    a->paned = GTK_PANED(gtk_paned_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_paned_set_start_child(a->paned, a->sidebar);
    gtk_paned_set_end_child(a->paned, GTK_WIDGET(wv));
    gtk_paned_set_position(a->paned, a->sidebar_pos);
    gtk_paned_set_shrink_start_child(a->paned, FALSE);
    gtk_widget_set_hexpand(GTK_WIDGET(a->paned), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(a->paned), TRUE);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    a->find_bar = build_find_bar(a);
    gtk_box_append(GTK_BOX(root), a->find_bar);
    gtk_box_append(GTK_BOX(root), GTK_WIDGET(a->paned));
    gtk_window_set_child(a->window, root);

    /* drag & drop files anywhere in the window */
    GtkDropTarget *dt = gtk_drop_target_new(G_TYPE_STRING, GDK_ACTION_COPY);
    g_signal_connect(dt, "drop", G_CALLBACK(on_drop), a);
    gtk_widget_add_controller(root, GTK_EVENT_CONTROLLER(dt));

    /* ---- state ---- */
    gtk_widget_set_visible(a->sidebar, a->sidebar_visible);
    gtk_toggle_button_set_active(a->sidebar_btn, a->sidebar_visible);
    set_sidebar_visible(a, a->sidebar_visible);

    if (a->last_dir)
        a->dir = g_file_new_for_path(a->last_dir);
    else
        a->dir = g_file_new_for_path(g_get_home_dir());
    refresh_file_list(a);

    g_signal_connect(a->window, "close-request",
                     G_CALLBACK(on_close_request), a);
}

/* ------------------------------------------------------------------ */
/* menu and actions                                                    */
/* ------------------------------------------------------------------ */

/* Built once; lives for the process lifetime (the menu button owns a ref to
 * the top level model). */
static GMenu *main_menu;

static GMenu *build_menu(App *a)
{
    if (main_menu) return main_menu;

    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Open…", "app.open");
    g_menu_append(menu, "Find in Page…", "app.find");

    GMenu *zoom = g_menu_new();
    g_menu_append(zoom, "Zoom In", "app.zoomin");
    g_menu_append(zoom, "Zoom Out", "app.zoomout");
    g_menu_append(zoom, "Reset Zoom", "app.zoomreset");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(zoom));

    GMenu *view = g_menu_new();
    g_menu_append(view, "Sidebar", "app.sidebar");
    g_menu_append(view, "Fullscreen", "app.fullscreen");
    GMenu *theme = g_menu_new();
    g_menu_append(theme, "System", "app.theme::system");
    g_menu_append(theme, "Light", "app.theme::light");
    g_menu_append(theme, "Dark", "app.theme::dark");
    g_menu_append_submenu(view, "Theme", G_MENU_MODEL(theme));
    g_menu_append_submenu(menu, "View", G_MENU_MODEL(view));

    g_menu_append_section(menu, NULL, NULL);
    g_menu_append(menu, "About Markdown Viewer", "app.about");
    g_menu_append(menu, "Quit", "app.quit");

    main_menu = menu;
    return menu;
}

static const GActionEntry app_entries[] = {
    { "open",       action_open_cb,       NULL, NULL, NULL, { 0, 0, 0 } },
    { "find",       action_find_cb,       NULL, NULL, NULL, { 0, 0, 0 } },
    { "escape",     action_escape_cb,     NULL, NULL, NULL, { 0, 0, 0 } },
    { "fullscreen", action_fullscreen_cb, NULL, NULL, NULL, { 0, 0, 0 } },
    { "zoomin",     action_zoomin_cb,     NULL, NULL, NULL, { 0, 0, 0 } },
    { "zoomout",    action_zoomout_cb,    NULL, NULL, NULL, { 0, 0, 0 } },
    { "zoomreset",  action_zoomreset_cb,  NULL, NULL, NULL, { 0, 0, 0 } },
    { "about",      action_about_cb,      NULL, NULL, NULL, { 0, 0, 0 } },
    { "quit",       action_quit_cb,       NULL, NULL, NULL, { 0, 0, 0 } },
};

static void register_actions(App *a)
{
    g_action_map_add_action_entries(G_ACTION_MAP(a->gapp), app_entries,
                                    G_N_ELEMENTS(app_entries), a);

    a->theme_action = g_simple_action_new_stateful(
        "theme", G_VARIANT_TYPE_STRING, g_variant_new_string(a->theme));
    g_signal_connect(a->theme_action, "change-state",
                     G_CALLBACK(action_theme_cb), a);
    g_action_map_add_action(G_ACTION_MAP(a->gapp), G_ACTION(a->theme_action));

    a->sidebar_action = g_simple_action_new_stateful(
        "sidebar", NULL, g_variant_new_boolean(a->sidebar_visible));
    g_signal_connect(a->sidebar_action, "change-state",
                     G_CALLBACK(action_sidebar_cb), a);
    g_action_map_add_action(G_ACTION_MAP(a->gapp), G_ACTION(a->sidebar_action));

    gtk_application_set_accels_for_action(a->gapp, "app.open",
        (const char *[]){ "<Control>o", NULL });
    gtk_application_set_accels_for_action(a->gapp, "app.find",
        (const char *[]){ "<Control>f", NULL });
    gtk_application_set_accels_for_action(a->gapp, "app.escape",
        (const char *[]){ "Escape", NULL });
    gtk_application_set_accels_for_action(a->gapp, "app.fullscreen",
        (const char *[]){ "F11", NULL });
    gtk_application_set_accels_for_action(a->gapp, "app.sidebar",
        (const char *[]){ "<Control>b", NULL });
    gtk_application_set_accels_for_action(a->gapp, "app.zoomin",
        (const char *[]){ "<Control>plus", "<Control>equal", NULL });
    gtk_application_set_accels_for_action(a->gapp, "app.zoomout",
        (const char *[]){ "<Control>minus", NULL });
    gtk_application_set_accels_for_action(a->gapp, "app.zoomreset",
        (const char *[]){ "<Control>0", NULL });
    gtk_application_set_accels_for_action(a->gapp, "app.quit",
        (const char *[]){ "<Control>q", NULL });
}

/* ------------------------------------------------------------------ */
/* action callbacks                                                    */
/* ------------------------------------------------------------------ */

static void on_file_open(GObject *source, GAsyncResult *res, gpointer user_data)
{
    App *a = user_data;
    GError *err = NULL;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &err);

    if (f) {
        load_file(a, f);
        g_object_unref(f);
    } else if (err && a->window &&
               !g_error_matches(err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
               !g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        GtkWidget *d = GTK_WIDGET(gtk_alert_dialog_new("Unable to open file"));
        gtk_alert_dialog_set_detail(GTK_ALERT_DIALOG(d), err->message);
        gtk_alert_dialog_show(GTK_ALERT_DIALOG(d), a->window);
    }
    if (err) g_error_free(err);
}

static void action_open_cb(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    App *a = user_data;

    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, "Open Markdown File");
    gtk_file_dialog_set_modal(dlg, TRUE);

    GtkFileFilter *md = gtk_file_filter_new();
    gtk_file_filter_set_name(md, "Markdown files");
    gtk_file_filter_add_mime_type(md, "text/markdown");
    gtk_file_filter_add_mime_type(md, "text/x-markdown");
    gtk_file_filter_add_pattern(md, "*.md");
    gtk_file_filter_add_pattern(md, "*.markdown");
    gtk_file_filter_add_pattern(md, "*.mdown");
    gtk_file_filter_add_pattern(md, "*.mkd");

    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, md);
    g_list_store_append(filters, all);
    gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dlg, md);
    g_object_unref(filters);

    if (a->dir)
        gtk_file_dialog_set_initial_folder(dlg, a->dir);

    gtk_file_dialog_open(dlg, a->window, NULL, on_file_open, a);
    g_object_unref(dlg);
}

static void action_find_cb(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    App *a = user_data;

    gtk_widget_set_visible(a->find_bar, TRUE);
    gtk_widget_grab_focus(a->find_entry);
    gtk_editable_select_region(GTK_EDITABLE(a->find_entry), 0, -1);
}

static void action_escape_cb(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    App *a = user_data;
    on_find_close(a);
}

static void action_fullscreen_cb(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    App *a = user_data;
    set_fullscreen(a, !a->fullscreen);
}

static void action_zoomin_cb(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    App *a = user_data;
    a->zoom = CLAMP(a->zoom * 1.15, 0.5, 3.0);
    webkit_web_view_set_zoom_level(a->webview, a->zoom);
    save_settings(a);
}

static void action_zoomout_cb(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    App *a = user_data;
    a->zoom = CLAMP(a->zoom / 1.15, 0.5, 3.0);
    webkit_web_view_set_zoom_level(a->webview, a->zoom);
    save_settings(a);
}

static void action_zoomreset_cb(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    App *a = user_data;
    a->zoom = 1.0;
    webkit_web_view_set_zoom_level(a->webview, a->zoom);
    save_settings(a);
}

static void action_sidebar_cb(GSimpleAction *action, GVariant *value, gpointer user_data)
{
    App *a = user_data;
    gboolean v = a->sidebar_visible;
    if (value && g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN))
        v = g_variant_get_boolean(value);
    else
        v = !v;
    set_sidebar_visible(a, v);
}

static void action_theme_cb(GSimpleAction *action, GVariant *value, gpointer user_data)
{
    App *a = user_data;
    const char *t = g_variant_get_string(value, NULL);
    if (!t) return;

    g_free(a->theme);
    a->theme = g_strdup(t);
    g_simple_action_set_state(action, g_variant_new_string(t));

    apply_theme_js(a);
    save_settings(a);
}

static void action_about_cb(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    App *a = user_data;

    GtkWidget *d = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(d), "Markdown Viewer");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(d), APP_VERSION);
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(d),
        "A distraction-free viewer for Markdown files.");
    gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(d), GTK_LICENSE_MIT_X11);
    gtk_window_set_transient_for(GTK_WINDOW(d), a->window);
    gtk_window_present(GTK_WINDOW(d));
}

static void action_quit_cb(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    App *a = user_data;
    save_settings(a);
    g_application_quit(G_APPLICATION(a->gapp));
}

static void on_sidebar_btn(GtkToggleButton *btn, App *a)
{
    set_sidebar_visible(a, gtk_toggle_button_get_active(btn));
}

/* ------------------------------------------------------------------ */
/* find                                                                */
/* ------------------------------------------------------------------ */

static void on_find_activate(GtkSearchEntry *entry, App *a)
{
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    WebKitFindController *fc = webkit_web_view_get_find_controller(a->webview);
    const char *cur = webkit_find_controller_get_search_text(fc);

    if (cur && text && strcmp(cur, text) == 0)
        webkit_find_controller_search_next(fc);
    else if (text && *text)
        webkit_find_controller_search(fc, text,
            WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND,
            G_MAXUINT);
}

static void on_find_next(GtkButton *btn, App *a)
{
    (void)btn;
    webkit_find_controller_search_next(
        webkit_web_view_get_find_controller(a->webview));
}

static void on_find_previous(GtkButton *btn, App *a)
{
    (void)btn;
    webkit_find_controller_search_previous(
        webkit_web_view_get_find_controller(a->webview));
}

static void on_find_close(App *a)
{
    gtk_widget_set_visible(a->find_bar, FALSE);
    webkit_find_controller_search_finish(
        webkit_web_view_get_find_controller(a->webview));
}

/* ------------------------------------------------------------------ */
/* web view signals                                                    */
/* ------------------------------------------------------------------ */

static void on_load_changed(WebKitWebView *wv, WebKitLoadEvent event, App *a)
{
    (void)wv;
    if (event == WEBKIT_LOAD_FINISHED)
        apply_theme_js(a);
}

static void uri_launch_done(GObject *source, GAsyncResult *res, gpointer user_data)
{
    (void)user_data;
    GError *err = NULL;
    if (!gtk_uri_launcher_launch_finish(GTK_URI_LAUNCHER(source), res, &err) && err)
        g_error_free(err);
}

static gboolean on_decide_policy(WebKitWebView *wv, WebKitPolicyDecision *decision,
                                 WebKitPolicyDecisionType decision_type, App *a)
{
    (void)wv;

    if (decision_type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
        return FALSE;

    WebKitNavigationPolicyDecision *nd = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    WebKitNavigationAction *action = webkit_navigation_policy_decision_get_navigation_action(nd);
    if (webkit_navigation_action_get_navigation_type(action) !=
        WEBKIT_NAVIGATION_TYPE_LINK_CLICKED)
        return FALSE;

    WebKitURIRequest *req = webkit_navigation_action_get_request(action);
    const char *uri = webkit_uri_request_get_uri(req);
    if (!uri) return FALSE;

    /* external links -> default browser */
    if (!g_str_has_prefix(uri, "file://") && !g_str_has_prefix(uri, "about:") &&
        !g_str_has_prefix(uri, "data:")) {
        GtkUriLauncher *l = gtk_uri_launcher_new(uri);
        gtk_uri_launcher_launch(l, a->window, NULL, uri_launch_done, NULL);
        g_object_unref(l);
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }

    /* links to other Markdown files -> render them in the viewer */
    char *tmp = g_strdup(uri);
    char *hash = strchr(tmp, '#');
    gboolean has_frag = hash != NULL;
    if (hash) *hash = '\0';

    gboolean handled = FALSE;
    if (g_str_has_prefix(tmp, "file://")) {
        char *path = g_filename_from_uri(tmp, NULL, NULL);
        if (path) {
            if (is_markdown_path(path)) {
                gboolean self_anchor = has_frag && a->file &&
                    g_strcmp0(g_file_get_path(a->file), path) == 0;
                if (!self_anchor) {
                    GFile *f = g_file_new_for_path(path);
                    load_file(a, f);
                    g_object_unref(f);
                    webkit_policy_decision_ignore(decision);
                    handled = TRUE;
                }
            }
            g_free(path);
        }
    }
    g_free(tmp);
    return handled;
}

static gboolean on_drop(GtkDropTarget *target, const GValue *value,
                        double x, double y, App *a)
{
    (void)target; (void)x; (void)y;

    const char *s = g_value_get_string(value);
    if (!s) return FALSE;

    char *first = g_strdup(s);
    char *nl = strchr(first, '\n');
    if (nl) *nl = '\0';

    GFile *f = NULL;
    if (g_str_has_prefix(first, "file://"))
        f = g_file_new_for_uri(first);
    else if (g_file_test(first, G_FILE_TEST_EXISTS))
        f = g_file_new_for_path(first);

    if (f) {
        load_file(a, f);
        g_object_unref(f);
    }
    g_free(first);
    return f != NULL;
}

static gboolean on_close_request(GtkWindow *window, App *a)
{
    (void)window;
    if (a->paned)
        a->sidebar_pos = gtk_paned_get_position(a->paned);
    save_settings(a);
    return FALSE;   /* let the window close */
}

/* ------------------------------------------------------------------ */
/* application                                                         */
/* ------------------------------------------------------------------ */

static void ensure_window(App *a)
{
    if (a->window) {
        gtk_window_present(a->window);
        return;
    }
    build_ui(a);
    gtk_window_present(a->window);
}

static void app_activate(GtkApplication *gapp, gpointer user_data)
{
    (void)gapp;
    App *a = user_data;
    ensure_window(a);
    if (!a->file)
        show_welcome(a);
}

static void app_open(GtkApplication *gapp, GFile **files, gint n_files,
                     const gchar *hint, gpointer user_data)
{
    (void)gapp; (void)hint;
    App *a = user_data;
    ensure_window(a);
    if (n_files > 0)
        load_file(a, files[0]);
}

/* ------------------------------------------------------------------ */
/* headless rendering (verification / static export)                   */
/* ------------------------------------------------------------------ */

static int render_cli(const char *in, const char *out, const char *theme)
{
    char *data = NULL;
    gsize len = 0;

    if (!g_file_get_contents(in, &data, &len, NULL)) {
        fprintf(stderr, "mdview: cannot read '%s'\n", in);
        return 1;
    }

    char *name = g_path_get_basename(in);
    char *html = mdview_render_html(data, len, theme, name);
    gboolean ok = g_file_set_contents(out, html, -1, NULL);

    if (!ok) fprintf(stderr, "mdview: cannot write '%s'\n", out);

    g_free(html);
    g_free(name);
    g_free(data);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    App a;
    memset(&a, 0, sizeof a);
    a.theme = g_strdup("system");
    a.zoom = 1.0;
    a.width = 1280;
    a.height = 820;
    a.sidebar_pos = 260;
    a.sidebar_visible = TRUE;

    load_settings(&a);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--render") == 0 && i + 2 < argc)
            return render_cli(argv[i + 1], argv[i + 2], a.theme);
        if (strcmp(argv[i], "--version") == 0) {
            printf("mdview %s\n", APP_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: mdview [FILE…]\n"
                   "       mdview --render INPUT.md OUTPUT.html\n"
                   "\n"
                   "A standalone Markdown viewer (GTK 4 + WebKitGTK).\n");
            return 0;
        }
    }

    GtkApplication *gapp = gtk_application_new(APP_ID, G_APPLICATION_HANDLES_OPEN);
    a.gapp = gapp;

    register_actions(&a);

    g_signal_connect(gapp, "activate", G_CALLBACK(app_activate), &a);
    g_signal_connect(gapp, "open", G_CALLBACK(app_open), &a);

    int status = g_application_run(G_APPLICATION(gapp), argc, argv);

    save_settings(&a);
    g_object_unref(gapp);
    return status;
}
