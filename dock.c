// MIT License
// Lukas Veselovsky, lukve
//
// Created with magician help of the AI.
//

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

#define ICON_SIZE 52
#define MARGIN 8
#define BOTTOM_OFFSET 12

GtkWidget *main_box;
GtkWidget *dock_window;
GHashTable *active_apps;

const char *CSS_STYLE = 
    "#my-dock-window {"
    "   background-color: rgba(30, 30, 30, 0.75);"
    "   border-radius: 16px;"
    "   border: 1px solid rgba(255, 255, 255, 0.1);"
    "}"
    "/* Voliteľný štýlový bonus: odlíšenie spúšťača od bežiacich appiek */"
    ".launcher-btn {"
    "   opacity: 0.9;"
    "}"
    ".launcher-btn:hover {"
    "   opacity: 1.0;"
    "   background-color: rgba(255, 255, 255, 0.1);"
    "   border-radius: 8px;"
    "}";

GdkPixbuf* scale_pixbuf(GdkPixbuf *pixbuf) {
    if (!pixbuf) return NULL;
    return gdk_pixbuf_scale_simple(pixbuf, ICON_SIZE, ICON_SIZE, GDK_INTERP_BILINEAR);
}

void keep_centered_at_bottom(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data) {
    GdkDisplay *display = gdk_display_get_default();
    GdkWindow *gdk_win = gtk_widget_get_window(widget);
    if (!gdk_win) return;

    GdkMonitor *monitor = gdk_display_get_monitor_at_window(display, gdk_win);
    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);

    int center_x = geometry.x + (geometry.width - allocation->width) / 2;
    int bottom_y = geometry.y + geometry.height - allocation->height - BOTTOM_OFFSET;

    gtk_window_move(GTK_WINDOW(widget), center_x, bottom_y);
}

void on_button_clicked(GtkWidget *button, gpointer data) {
    WnckWindow *window = WNCK_WINDOW(data);
    GdkWindow *gdk_win = gtk_widget_get_window(dock_window);
    guint32 timestamp = gdk_x11_get_server_time(gdk_win);

    if (wnck_window_is_active(window)) {
        wnck_window_minimize(window);
    } else {
        wnck_window_activate(window, timestamp);
    }
}

// Akcia pre fixnú ikonu spúšťača
void on_launcher_clicked(GtkWidget *button, gpointer command) {
    GError *error = NULL;
    // Asynchrónne spustenie príkazu (napr. "xterm") na pozadí
    if (!g_spawn_command_line_async((const char *)command, &error)) {
        g_warning("Nepodarilo sa spustiť príkaz: %s", error->message);
        g_error_free(error);
    }
}

// Pomocná funkcia na pridanie fixnej ikony (spúšťača) na začiatok docku
void add_fixed_launcher(const char *icon_name, const char *command) {
    GtkWidget *button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    
    // Pridáme CSS triedu pre možnosť špecifického štýlovania hover efektu
    GtkStyleContext *context = gtk_widget_get_style_context(button);
    gtk_style_context_add_class(context, "launcher-btn");

    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    GdkPixbuf *pixbuf = gtk_icon_theme_load_icon(icon_theme, icon_name, ICON_SIZE, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
    
    GtkWidget *image;
    if (pixbuf) {
        GdkPixbuf *scaled = scale_pixbuf(pixbuf);
        image = gtk_image_new_from_pixbuf(scaled);
        g_object_unref(pixbuf);
        g_object_unref(scaled);
    } else {
        image = gtk_image_new_from_icon_name("utilities-terminal", GTK_ICON_SIZE_DIALOG);
    }

    gtk_container_add(GTK_CONTAINER(button), image);
    
    // Prepojíme kliknutie na spustenie príkazu (v tomto prípade odovzdáme reťazec "xterm")
    g_signal_connect(button, "clicked", G_CALLBACK(on_launcher_clicked), (gpointer)command);

    // Vložíme na začiatok hlavného boxu
    gtk_box_pack_start(GTK_BOX(main_box), button, FALSE, FALSE, 0);
}

void on_window_opened(WnckScreen *screen, WnckWindow *window, gpointer user_data) {
    if (wnck_window_get_window_type(window) != WNCK_WINDOW_NORMAL) return;

    GtkWidget *button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);

    GdkPixbuf *raw_icon = wnck_window_get_icon(window);
    GdkPixbuf *scaled_icon = scale_pixbuf(raw_icon);
    
    GtkWidget *image;
    if (scaled_icon) {
        image = gtk_image_new_from_pixbuf(scaled_icon);
        g_object_unref(scaled_icon);
    } else {
        image = gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_DIALOG);
    }

    gtk_container_add(GTK_CONTAINER(button), image);
    g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), window);

    // Bežiace aplikácie sa budú zaraďovať za náš fixný launcher
    gtk_box_pack_start(GTK_BOX(main_box), button, FALSE, FALSE, 0);
    gtk_widget_show_all(button);

    g_hash_table_insert(active_apps, window, button);
}

void on_window_closed(WnckScreen *screen, WnckWindow *window, gpointer user_data) {
    GtkWidget *button = g_hash_table_lookup(active_apps, window);
    if (button) {
        gtk_container_remove(GTK_CONTAINER(main_box), button);
        g_hash_table_remove(active_apps, window);
    }
}

void enable_alpha_channel(GtkWidget *window) {
    GdkScreen *gdk_screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(gdk_screen);
    if (visual != NULL && gdk_screen_is_composited(gdk_screen)) {
        gtk_widget_set_visual(window, visual);
    }
}

void apply_css_style(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CSS_STYLE, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    active_apps = g_hash_table_new(g_direct_hash, g_direct_equal);

    dock_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    enable_alpha_channel(dock_window);
    gtk_widget_set_name(dock_window, "my-dock-window");

    gtk_window_set_type_hint(GTK_WINDOW(dock_window), GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_window_set_decorated(GTK_WINDOW(dock_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(dock_window), FALSE);

    apply_css_style();

    main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, MARGIN);
    gtk_widget_set_margin_top(main_box, MARGIN);
    gtk_widget_set_margin_bottom(main_box, MARGIN);
    gtk_widget_set_margin_start(main_box, MARGIN);
    gtk_widget_set_margin_end(main_box, MARGIN);
    gtk_container_add(GTK_CONTAINER(dock_window), main_box);

    // --- PRIDANIE TRVALÉHO SPÚŠŤAČA ---
    // Parameter 1: Názov systémovej ikony pre terminál (zvyčajne "utilities-terminal" alebo "xterm")
    // Parameter 2: Príkaz, ktorý sa reálne vykoná v systéme
    add_fixed_launcher("utilities-terminal", "xterm");

    // Inicializácia sledovania okien
    WnckScreen *screen = wnck_screen_get_default();
    wnck_screen_force_update(screen);

    g_signal_connect(screen, "window-opened", G_CALLBACK(on_window_opened), NULL);
    g_signal_connect(screen, "window-closed", G_CALLBACK(on_window_closed), NULL);
    g_signal_connect(dock_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(dock_window, "size-allocate", G_CALLBACK(keep_centered_at_bottom), NULL);

    gtk_widget_show_all(dock_window);
    gtk_main();

    g_hash_table_destroy(active_apps);
    return 0;
}
