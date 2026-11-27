#include <gtk/gtk.h>
#include <stdio.h>
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    const char *root = g_getenv("APPDIR"), *cache = g_getenv("GDK_PIXBUF_MODULE_FILE");
    if (!root || !cache || !g_file_test(cache, G_FILE_TEST_IS_REGULAR)) return 1;
    gchar *svg = g_build_filename(root,"usr","share","wupstreamd","icons","wiiu","btn_a.svg",NULL);
    GError *error = NULL;
    GdkPixbuf *pix = gdk_pixbuf_new_from_file(svg,&error);
    g_free(svg);
    if (!pix) { fprintf(stderr,"SVG: %s\n",error ? error->message : "unknown"); g_clear_error(&error); return 1; }
    g_object_unref(pix);
    GtkIconTheme *theme = gtk_icon_theme_new();
    gchar *icons = g_build_filename(root,"usr","share","icons",NULL);
    const gchar *paths[] = {icons};
    gtk_icon_theme_set_search_path(theme,paths,1);
    gtk_icon_theme_set_custom_theme(theme,"Adwaita");
    g_free(icons);
    const char *names[] = {"window-close-symbolic","window-minimize-symbolic","preferences-system-symbolic","document-open-symbolic"};
    for (gsize i=0;i<G_N_ELEMENTS(names);i++) {
        pix=gtk_icon_theme_load_icon(theme,names[i],16,GTK_ICON_LOOKUP_FORCE_SIZE,&error);
        if (!pix) { fprintf(stderr,"%s: %s\n",names[i],error ? error->message : "unknown"); g_clear_error(&error); g_object_unref(theme); return 1; }
        g_object_unref(pix);
    }
    g_object_unref(theme);
    puts("PASS: bundled SVG and GTK symbolic icons"); return 0;
}
