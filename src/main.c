#define _DEFAULT_SOURCE
#include "settings.h"
#include "server.h"
#include "ui.h"
#include "kms_permissions.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>

/* install a user autostart entry pointing at this binary (once) */
static void ensure_autostart(void) {
    char exe[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;
    exe[n] = 0;
    const char *home = g_get_home_dir();
    char dir[600], path[700], content[1200];
    snprintf(dir, sizeof dir, "%s/.config/autostart", home);
    snprintf(path, sizeof path, "%s/wupstreamd.desktop", dir);
    if (g_file_test(path, G_FILE_TEST_EXISTS)) return;
    g_mkdir_with_parents(dir, 0700);
    snprintf(content, sizeof content,
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=wupstreamd\n"
        "Comment=Stream your screen to a Wii U GamePad\n"
        "Exec=%s\n"
        "Icon=wupstreamd\n"
        "StartupWMClass=wupstreamd\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n", exe);
    g_file_set_contents(path, content, -1, NULL);
}

/* refuse to run twice: flock on a lock file, held for the process lifetime */
static int acquire_instance_lock(void) {
    char path[600];
    snprintf(path, sizeof path, "%s/.config/wup-stream-serveur/wupstreamd.lock",
             g_get_home_dir());
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    int fd = open(path, O_RDWR | O_CREAT, 0600);
    if (fd < 0) return 0;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); return -1; }
    return fd;   /* keep open; released on exit */
}

int main(int argc, char **argv) {
    /* WUP_V915_TASKBAR_ICON: stable X11 WM_CLASS/desktop identity.
     * Use the .desktop filename stem, not the window title or argv[0]. */
    g_set_prgname("wupstreamd");
    gtk_init(&argc, &argv);
    gdk_set_program_class("wupstreamd");

    int lockfd = acquire_instance_lock();
    if (lockfd < 0) {
        /* GSpawn is not a shell: redirection text was passed as arguments. */
        g_spawn_command_line_async(
            "notify-send 'wupstreamd' 'Already running'", NULL);
        return 0;
    }

    settings_t *s = settings_load();
    server_t *srv = server_new(s);
    ui_t *ui = ui_new(s, srv);
    int kms_ready = 1;
    if (s->capture_backend >= 2 && !kms_permissions_ready()) {
        wup_i18n_t *tr = wup_i18n_new(s->language_id);
        kms_ready = kms_permissions_ensure(NULL,tr);
        wup_i18n_free(tr);
    }
    server_t *srv = server_new(s);
    ui_t *ui = ui_new(s, srv);

    ensure_autostart();

    if (s->autostart_stream && kms_ready)
        server_request_stream(srv);

    gtk_main();

    server_destroy(srv);
    settings_save(s);
    ui_free(ui);
    return 0;
}
