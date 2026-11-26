#define _GNU_SOURCE
#include "kms_permissions.h"
#include "kms_wire.h"
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <glib/gstdio.h>

/* Do not install any persistent Polkit rule or authorize unattended updates.
 * The user must explicitly trust the application containing the helper. */
static const char *t(const wup_i18n_t *tr,const char *s) {
    return tr ? wup_i18n_lookup(tr,s) : s;
}
/* Host tools must not inherit the AppImage library search path. */
static gchar **host_env(void) {
    gchar **env=g_get_environ();
    const char *names[]={"LD_LIBRARY_PATH","LD_PRELOAD","LD_AUDIT","GIO_EXTRA_MODULES","GIO_MODULE_DIR","GTK_PATH","GTK_IM_MODULE_FILE","GDK_PIXBUF_MODULE_FILE",NULL};
    for(int i=0;names[i];i++) env=g_environ_unsetenv(env,names[i]);
    return env;
}
static gboolean host_spawn(gchar **argv,gchar **out,gchar **err,gint *status,GError **error) {
    gchar **env=host_env();
    gboolean ok=g_spawn_sync(NULL,argv,env,0,NULL,NULL,out,err,status,error);
    g_strfreev(env);return ok;
}
static int installed_permissions_ready(void) {
    struct stat st;
    if (lstat(KMS_HELPER_PATH,&st) || !S_ISREG(st.st_mode) || st.st_uid!=0 ||
        (st.st_mode & 0022) || access(KMS_HELPER_PATH,X_OK)) return 0;
    const char *captool=g_file_test("/usr/sbin/getcap",G_FILE_TEST_IS_EXECUTABLE)?
        "/usr/sbin/getcap":"/sbin/getcap";
    gchar *argv[]={(gchar *)captool,(gchar *)KMS_HELPER_PATH,NULL};
    gchar *out=NULL; gint status=0;
    gboolean ok=host_spawn(argv,&out,NULL,&status,NULL);
    gchar *expected=g_strconcat(KMS_HELPER_PATH," cap_sys_admin=ep",NULL);
    int ready=ok && g_spawn_check_wait_status(status,NULL) && out &&
        !strcmp(g_strstrip(out),expected);
    g_free(expected); g_free(out); return ready;
}
int kms_permissions_ready(void) {
    if (!installed_permissions_ready()) return 0;
    gchar *argv[]={(gchar *)KMS_HELPER_PATH,"--version",NULL};
    gchar *out=NULL; gint status=0;
    gboolean ok=host_spawn(argv,&out,NULL,&status,NULL);
    int ready=ok && g_spawn_check_wait_status(status,NULL) && out &&
        !strcmp(g_strstrip(out),KMS_HELPER_VERSION);
    g_free(out); return ready;
}

static void theme_dialog(GtkWidget *d) {
    gtk_window_set_decorated(GTK_WINDOW(d), FALSE);
    gtk_window_set_position(GTK_WINDOW(d), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_resizable(GTK_WINDOW(d), FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(d), "wup-kms-dialog");
    GtkStyleContext *ctx = gtk_widget_get_style_context(d);
    gtk_style_context_add_class(ctx, "wup-settings");
    gtk_widget_set_name(d, "wup-settings");
}

static void notice(GtkWindow *parent,const wup_i18n_t *tr,const char *message,const char *detail) {
    GtkWidget *d=gtk_message_dialog_new(parent,GTK_DIALOG_MODAL,GTK_MESSAGE_ERROR,GTK_BUTTONS_CLOSE,"%s",t(tr,message));
    theme_dialog(d);
    if(detail && *detail) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),"%s",detail);
    gtk_style_context_add_class(gtk_widget_get_style_context(d), "wup-settings");
    gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d);
}
static int install_helper(GtkWindow *parent,const wup_i18n_t *tr,int force) {
    if(!force && kms_permissions_ready()) return 1;
    gchar *exe=g_file_read_link("/proc/self/exe",NULL);
    gchar *dir=exe?g_path_get_dirname(exe):NULL;
    gchar *source=dir?g_build_filename(dir,"wup-kms-helper",NULL):NULL;
    g_free(dir); g_free(exe);
    int encoded=0;
    if(source && !g_file_test(source,G_FILE_TEST_IS_REGULAR)) {
        gchar *payload=g_strconcat(source,".b64",NULL);
        g_free(source);source=payload;encoded=1;
    }
    if(!source || !g_file_test(source,G_FILE_TEST_IS_REGULAR) ||
       !g_file_test("/usr/bin/pkexec",G_FILE_TEST_IS_EXECUTABLE)) {
        notice(parent,tr,"KMS installation unavailable","Missing bundled helper or /usr/bin/pkexec.");
        g_free(source); return 0;
    }
    GtkWidget *d=gtk_message_dialog_new(parent,GTK_DIALOG_MODAL,GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,"%s",t(tr,installed_permissions_ready()?"Update KMS helper":"Enable DRM/KMS capture"));
    theme_dialog(d);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),"%s",t(tr,"Install a privileged screen capture helper from this application? Only continue if you trust this build. Administrator authentication is required once. XCB does not require it."));
    gtk_dialog_add_buttons(GTK_DIALOG(d),t(tr,"Cancel"),GTK_RESPONSE_CANCEL,t(tr,"Install KMS helper"),GTK_RESPONSE_ACCEPT,NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);
    gtk_style_context_add_class(gtk_widget_get_style_context(d), "wup-settings");
    int answer=gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d);
    if(answer!=GTK_RESPONSE_ACCEPT) {g_free(source);return 0;}
    gchar *bytes=NULL; gsize size=0; GError *error=NULL;
    if(!g_file_get_contents(source,&bytes,&size,&error)) {
        notice(parent,tr,"KMS installation failed",error?error->message:NULL);
        g_clear_error(&error);g_free(source);return 0;
    }
    if(encoded) {
        gsize decoded_size=0;
        guchar *decoded=g_base64_decode(bytes,&decoded_size);
        g_free(bytes);bytes=(gchar *)decoded;size=decoded_size;
    }
    /* FUSE mounts are not necessarily accessible to the authenticated root
     * process. Stage the exact native ELF in a private normal directory. */
    gchar *stage=g_dir_make_tmp("wup-kms-install-XXXXXX",&error);
    gchar *staged=stage?g_build_filename(stage,"wup-kms-helper",NULL):NULL;
    if(!staged || size<4 || memcmp(bytes,"\177ELF",4) ||
       !g_file_set_contents(staged,bytes,size,&error)) {
        notice(parent,tr,"KMS installation failed",error?error->message:"Invalid helper payload");
        if(staged) g_unlink(staged);
        if(stage) g_rmdir(stage);
        g_free(staged);g_free(stage);g_free(bytes);g_free(source);g_clear_error(&error);return 0;
    }
    gchar *digest=g_compute_checksum_for_data(G_CHECKSUM_SHA256,(const guchar *)bytes,size);
    g_free(bytes);
    /* pkexec authenticates this explicit installation only. No shell input
     * interpolation: source and digest are positional arguments. Stage in a
     * protected directory and atomically replace the installed executable. */
    static const char script[]=
        "set -eu; umask 077; "
        "d=/usr/local/libexec/wupstreamd; "
        "for p in /usr/local /usr/local/libexec \"$d\"; do "
        "if [ ! -e \"$p\" ]; then /usr/bin/install -d -o root -g root -m 0755 \"$p\"; fi; "
        "[ -d \"$p\" ] && [ ! -L \"$p\" ] && [ \"$(/usr/bin/stat -c %u \"$p\")\" = 0 ]; "
        "m=$(/usr/bin/stat -c %a \"$p\"); [ $((0$m & 022)) -eq 0 ]; done; "
        "tmp=$(/usr/bin/mktemp \"$d/.helper.XXXXXXXX\"); trap '/usr/bin/rm -f -- \"$tmp\"' EXIT; "
        "/usr/bin/install -o root -g video -m 0750 -- \"$1\" \"$tmp\"; "
        "actual=$(/usr/bin/sha256sum \"$tmp\"); [ \"${actual%% *}\" = \"$2\" ]; "
        "/usr/sbin/setcap cap_sys_admin+ep \"$tmp\"; "
        "/usr/bin/mv -fT -- \"$tmp\" \"$d/wup-kms-helper\"; "
        "trap - EXIT";
    gchar *argv[]={"/usr/bin/pkexec","/bin/sh","-c",(gchar *)script,"wup-kms-install",staged,digest,NULL};
    gint status=0; gchar *err=NULL;
    gboolean ok=host_spawn(argv,NULL,&err,&status,&error);
    g_unlink(staged);g_rmdir(stage);g_free(staged);g_free(stage);
    int ready=ok && g_spawn_check_wait_status(status,NULL) && kms_permissions_ready();
    if(!ready) notice(parent,tr,"KMS installation failed",error?error->message:err);
    g_clear_error(&error);g_free(err);g_free(digest);g_free(source);return ready;
}
int kms_permissions_ensure(GtkWindow *parent,const wup_i18n_t *tr) {
    return install_helper(parent,tr,0);
}
void kms_permissions_manage(GtkWindow *parent,const wup_i18n_t *tr) {
    int present=g_file_test(KMS_HELPER_PATH,G_FILE_TEST_EXISTS);
    GtkWidget *d=gtk_message_dialog_new(parent,GTK_DIALOG_MODAL,GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,"%s",t(tr,"KMS helper maintenance"));
    theme_dialog(d);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),"%s",t(tr,"Install or update the privileged helper, or remove it. Stop all KMS streams before maintenance. Removal prevents new helper launches; existing processes must be stopped separately."));
    gtk_dialog_add_buttons(GTK_DIALOG(d),t(tr,"Cancel"),GTK_RESPONSE_CANCEL,t(tr,"Install or update"),GTK_RESPONSE_ACCEPT,NULL);
    if(present) gtk_dialog_add_button(GTK_DIALOG(d),t(tr,"Remove KMS helper"),GTK_RESPONSE_REJECT);
    gtk_dialog_set_default_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);
    gtk_style_context_add_class(gtk_widget_get_style_context(d), "wup-settings");
    int answer=gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d);
    if(answer==GTK_RESPONSE_ACCEPT) { install_helper(parent,tr,1); return; }
    if(answer!=GTK_RESPONSE_REJECT) return;
    static const char script[]=
        "set -eu; for p in /usr/local /usr/local/libexec /usr/local/libexec/wupstreamd; do "
        "[ -d \"$p\" ] && [ ! -L \"$p\" ] && [ \"$(/usr/bin/stat -c %u \"$p\")\" = 0 ]; "
        "m=$(/usr/bin/stat -c %a \"$p\"); [ $((0$m & 022)) -eq 0 ]; done; "
        "/usr/bin/rm -f -- /usr/local/libexec/wupstreamd/wup-kms-helper";
    gchar *argv[]={"/usr/bin/pkexec","/bin/sh","-c",(gchar *)script,NULL};
    gint status=0; gchar *err=NULL; GError *error=NULL;
    gboolean ok=host_spawn(argv,NULL,&err,&status,&error);
    if(!ok || !g_spawn_check_wait_status(status,NULL) || g_file_test(KMS_HELPER_PATH,G_FILE_TEST_EXISTS))
        notice(parent,tr,"KMS removal failed",error?error->message:err);
    g_clear_error(&error);g_free(err);
}
