/* js_markup_probe.c — verify the WebKit security configuration used by mdview:
 *
 *   enable_javascript        = TRUE   (the application drives the page)
 *   enable_javascript_markup = FALSE  (document markup scripts must not run)
 *
 * Usage: js_markup_probe test.html
 * Prints SCRIPT-RAN, SCRIPT-BLOCKED or EVAL-ERROR.
 */
#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include <stdio.h>

static GMainLoop *loop;

static void done(GObject *source, GAsyncResult *res, gpointer data)
{
    (void)data;
    GError *err = NULL;
    JSCValue *v = webkit_web_view_evaluate_javascript_finish(
                     WEBKIT_WEB_VIEW(source), res, &err);

    if (err) {
        printf("EVAL-ERROR: %s\n", err->message);
        g_error_free(err);
    } else if (v) {
        printf("%s\n", jsc_value_is_string(v) ? jsc_value_to_string(v)
                                             : "NON-STRING");
        g_object_unref(v);
    } else {
        printf("NO-RESULT\n");
    }
    g_main_loop_quit(loop);
}

static void load_changed(WebKitWebView *wv, WebKitLoadEvent ev, gpointer data)
{
    (void)data;
    if (ev != WEBKIT_LOAD_FINISHED) return;

    webkit_web_view_evaluate_javascript(wv,
        "(()=>{const ran=(window.__pwned===1||window.__pwned===2);"
        "return ran?'SCRIPT-RAN':'SCRIPT-BLOCKED';})()",
        -1, NULL, NULL, NULL, done, NULL);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s test.html\n", argv[0]);
        return 2;
    }

    gtk_init();

    loop = g_main_loop_new(NULL, FALSE);
    WebKitWebView *wv = WEBKIT_WEB_VIEW(webkit_web_view_new());

    WebKitSettings *s = webkit_settings_new();
    webkit_settings_set_enable_javascript(s, TRUE);
    webkit_settings_set_enable_javascript_markup(s, FALSE);
    webkit_web_view_set_settings(wv, s);
    g_object_unref(s);

    g_signal_connect(wv, "load-changed", G_CALLBACK(load_changed), NULL);

    char *contents = NULL;
    if (!g_file_get_contents(argv[1], &contents, NULL, NULL)) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    webkit_web_view_load_html(wv, contents, NULL);
    g_free(contents);

    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
}
