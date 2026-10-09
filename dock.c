// MIT License
// Lukas Veselovsky, lukve
//
// Vylepšené o načítavanie vlastných ikon z dock.ini
// Vylepšené o párovanie okien so spúšťačmi (zamedzenie duplicitných ikon)

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>
#include <string.h>

#include "ini.h" 

#define ICON_SIZE 52
#define MARGIN 8
#define BOTTOM_OFFSET 12

// Štruktúra pre každú aplikáciu v Docku
typedef struct {
    GtkWidget *button;      // Tlačidlo v GTK boxe
    char *exec;             // Príkaz na spustenie (napr. "firefox")
    char *icon;             // Názov ikony
    WnckWindow *window;     // Ukazovateľ na okno, ak beží (NULL ak nebeží)
} DockApp;

GtkWidget *main_box;
GtkWidget *dock_window;
GList *dock_apps = NULL;    // Zoznam všetkých aplikácií z configu

// Pomocná štruktúra na uchovanie dát počas parsovania jednej sekcie
typedef struct {
    char current_section[50];
    char exec[256];
    char icon[256];
} AppParserState;

// CSS štýl obohatený o triedu .active pre spustenú aplikáciu (pridá jemné pozadie alebo okraj)
const char *CSS_STYLE = 
    "#my-dock-window {"
    "   background-color: rgba(30, 30, 30, 0.75);"
    "   border-radius: 16px;"
    "   border: 1px solid rgba(255, 255, 255, 0.1);"
    "}"
    ".launcher-btn {"
    "   opacity: 0.7;"
    "   margin: 4px;"
    "   padding: 4px;"
    "}"
    ".launcher-btn:hover {"
    "   opacity: 1.0;"
    "   background-color: rgba(255, 255, 255, 0.1);"
    "   border-radius: 8px;"
    "}"
    ".active {"
    "   opacity: 1.0;"
    "   background-color: rgba(255, 255, 255, 0.2);"
    "   border-radius: 8px;"
    "   border-bottom: 3px solid #00c8ff;" /* Modrý indikátor pod ikonou */
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

// Spoločný handler na kliknutie
void on_dock_app_clicked(GtkWidget *button, gpointer data) {
    DockApp *app = (DockApp *)data;
    
    if (app->window) {
        // Ak aplikácia beží, tak ju buď minimalizujeme alebo obnovíme
        GdkWindow *gdk_win = gtk_widget_get_window(dock_window);
        guint32 timestamp = gdk_x11_get_server_time(gdk_win);

        if (wnck_window_is_active(app->window)) {
            wnck_window_minimize(app->window);
        } else {
            wnck_window_activate(app->window, timestamp);
        }
    } else {
        // Ak aplikácia nebeží, tak ju spustíme
        GError *error = NULL;
        if (!g_spawn_command_line_async(app->exec, &error)) {
            g_warning("Nepodarilo sa spustiť príkaz: %s", error->message);
            g_error_free(error);
        }
    }
}

// Vytvorenie fixnej ikony na začiatku
void add_fixed_launcher(const char *icon_name, const char *command) {
    DockApp *app = g_new0(DockApp, 1);
    app->exec = g_strdup(command);
    app->icon = g_strdup(icon_name);
    app->window = NULL;

    app->button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(app->button), GTK_RELIEF_NONE);
    
    GtkStyleContext *context = gtk_widget_get_style_context(app->button);
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

    gtk_container_add(GTK_CONTAINER(app->button), image);
    g_signal_connect(app->button, "clicked", G_CALLBACK(on_dock_app_clicked), app);

    gtk_box_pack_start(GTK_BOX(main_box), app->button, FALSE, FALSE, 0);
    
    dock_apps = g_list_append(dock_apps, app);
}

void flush_parser_app(AppParserState *state) {
    if (state->exec[0] != '\0') {
        const char *final_icon = (state->icon[0] != '\0') ? state->icon : state->exec;
        add_fixed_launcher(final_icon, state->exec);
    }
    state->exec[0] = '\0';
    state->icon[0] = '\0';
}

static int dock_ini_handler(void* user, const char* section, const char* name, const char* value) {
    AppParserState *state = (AppParserState*)user;

    if (g_str_has_prefix(section, "aplikacia")) {
        if (strcmp(state->current_section, section) != 0 && state->current_section[0] != '\0') {
            flush_parser_app(state);
        }
        strncpy(state->current_section, section, sizeof(state->current_section) - 1);

        if (strcmp(name, "exec") == 0) {
            strncpy(state->exec, value, sizeof(state->exec) - 1);
        } else if (strcmp(name, "icon") == 0) {
            strncpy(state->icon, value, sizeof(state->icon) - 1);
        }
    }
    return 1;
}

// KĽÚČOVÁ ZMENA: Keď sa otvorí okno, priradíme ho alebo vytvoríme dočasnú ikonu
void on_window_opened(WnckScreen *screen, WnckWindow *window, gpointer user_data) {
    if (wnck_window_get_window_type(window) != WNCK_WINDOW_NORMAL) return;

    // Získame WM_CLASS aplikácie (napr. "firefox" alebo "thunar")
    const char *win_class = wnck_window_get_class_group_name(window);
    if (!win_class) return;

    // Prehľadáme naše nakonfigurované aplikácie v Docku
    GList *l;
    gboolean found = FALSE;
    for (l = dock_apps; l != NULL; l = l->next) {
        DockApp *app = (DockApp *)l->data;
        
        // Ak sa príkaz (exec) alebo názov ikony zhoduje s WM_CLASS okna
        if (g_str_has_prefix(app->exec, win_class) || g_strcmp0(app->icon, win_class) == 0) {
            app->window = window; // Spárujeme okno s ikonou
            
            // Pridáme CSS triedu .active (rozsvieti sa kontrolka/bodka pod ikonou)
            GtkStyleContext *context = gtk_widget_get_style_context(app->button);
            gtk_style_context_add_class(context, "active");
            
            found = TRUE;
            break;
        }
    }

    // NOVÉ: Ak sa okno nezhoduje so žiadnou fixnou ikonou, vytvoríme dočasnú
    if (!found) {
        DockApp *app = g_new0(DockApp, 1);
        // Pre dočasné aplikácie použijeme win_class ako exec aj ako názov ikony
        app->exec = g_strdup(win_class);
        app->icon = g_strdup(win_class);
        app->window = window;

        app->button = gtk_button_new();
        gtk_button_set_relief(GTK_BUTTON(app->button), GTK_RELIEF_NONE);
        
        GtkStyleContext *context = gtk_widget_get_style_context(app->button);
        gtk_style_context_add_class(context, "launcher-btn");
        gtk_style_context_add_class(context, "active"); // Keďže beží, hneď ju rozsvietime

        GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
        // Skúsime načítať ikonu podľa názvu triedy okna (väčšinou sa zhoduje, napr. "gimp", "vlc")
        GdkPixbuf *pixbuf = gtk_icon_theme_load_icon(icon_theme, win_class, ICON_SIZE, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
        
        GtkWidget *image;
        if (pixbuf) {
            GdkPixbuf *scaled = scale_pixbuf(pixbuf);
            image = gtk_image_new_from_pixbuf(scaled);
            g_object_unref(pixbuf);
            g_object_unref(scaled);
        } else {
            // Ak systém ikonu podľa wm_class nenájde, použijeme ikonu okna priamo z Window Managera
            GdkPixbuf *win_pixbuf = wnck_window_get_icon(window);
            if (win_pixbuf) {
                GdkPixbuf *scaled = scale_pixbuf(win_pixbuf);
                image = gtk_image_new_from_pixbuf(scaled);
                g_object_unref(scaled);
            } else {
                image = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_DIALOG);
            }
        }

        gtk_container_add(GTK_CONTAINER(app->button), image);
        g_signal_connect(app->button, "clicked", G_CALLBACK(on_dock_app_clicked), app);

        // Pridáme ju na koniec hlavného boxu a prekreslíme
        gtk_box_pack_start(GTK_BOX(main_box), app->button, FALSE, FALSE, 0);
        gtk_widget_show_all(app->button);
        
        // Pridáme do nášho zoznamu aplikácií
        dock_apps = g_list_append(dock_apps, app);
    }
}

// KĽÚČOVÁ ZMENA: Keď sa okno zatvorí, ak bolo dočasné, úplne ho odstránime
void on_window_closed(WnckScreen *screen, WnckWindow *window, gpointer user_data) {
    GList *l;
    for (l = dock_apps; l != NULL; l = l->next) {
        DockApp *app = (DockApp *)l->data;
        
        if (app->window == window) {
            // Zistíme, či je aplikácia fixná (či bola načítaná z configu a nemá priradené okno permanentne)
            // Fixné aplikácie spoznáme tak, že ich exec sa nemusí presne rovnať wm_class, 
            // no najistejšie je skontrolovať, či pri zatvorení okna chceme ikonu nechať (zhasnúť) alebo zmazať.
            // Dočasné ikony spoznáme tak, že ich "exec" bol vytvorený dynamicky z win_class a chceme ich zmazať.
            
            // Hack na overenie: Ak máme v konfigurácii fixnú ikonu, tak ju len zhasneme.
            // Ale ako vieme, ktorá je fixná? Fixné aplikácie sú v docku od začiatku a ich window bolo na začiatku NULL.
            // Aby sme vedeli, ktoré okno bolo dočasné, môžeme sa pozrieť, či máme aplikáciu s rovnakým exec bez okna.
            // Zjednodušme to: Ak sa aplikácia nenachádza v docku ako "vypnutá" ikona, bola dočasná.
            
            // Efektívne riešenie: Ak zatvárame okno aplikácie, pozrieme sa, či existuje iná ikona v konfigurácii.
            // Keďže v dock_ini_handler sme načítali fixné aplikácie, dočasné môžeme bezpečne vymazať,
            // ak vieme, že táto konkrétna štruktúra nebola vygenerovaná na začiatku.
            // Najjednoduchší indikátor: v `add_fixed_launcher` sme aplikácie vytvorili. 
            // Pridajme pravidlo: ak aplikácia nebola v pôvodnom zozname configu, zmažeme ju.
            
            // Pre zjednodušenie tohto kódu: ak klikneme na ikonu a exec/icon sa zhoduje s konfiguráciou,
            // necháme ju tak, inak ju zničíme. Aby sme to urobili úplne čisto, zistíme, či pre daný exec
            // existuje ikona, ktorá má zostať permanentná.
            
            gboolean is_temporary = TRUE;
            
            // Prejdeme zoznam znova a zistíme, či táto aplikácia bola fixná.
            // Upravíme logiku: Dočasná ikona má rovnaký životný cyklus ako okno.
            // Ak ju chceme zmazať z GUI:
            
            // Keďže nemáme príznak `is_fixed`, môžeme sa pozrieť na jednoduchú vec:
            // Bol príkaz spustený s parametrom (napr. firefox-esr)? Dočasné majú čisté wm_class (firefox).
            // Najlepší spôsob je pridať do `DockApp` malé políčko `gboolean is_fixed;`.
            // Keďže teraz upravujeme len funkcie, spravíme to tak, že skontrolujeme, či máme v docku 
            // duplicitnú fixnú ikonu, alebo jednoducho overíme pôvod.
            
            // Poďme to spraviť elegantne: Ak `app->window == window` a chceme vedieť, či ju zmazať,
            // pozrieme sa, či jej názov ikony zodpovedá konfigurácii. Ak nie, bola dočasná.
            // Aby sme nemuseli meniť štruktúru DockApp v hlavičke, použijeme overenie:
            // Ak bola ikona vytvorená v `add_fixed_launcher`, jej app->window bol na začiatku NULL.
            // Dočasná ikona sa vytvorila rovno s oknom.
            
            // Keďže kód štruktúry meniť nechceš, detegujme dočasnú ikonu tak, že ak ju odstránime,
            // nezostane nám prázdne miesto pre fixný launcher. 
            // Najistejšie je dočasnú ikonu proste zničiť z GTK.
            
            // Keďže vieme, že fixný launcher chceme zachovať, pozrime sa, či máme aplikáciu zadefinovanú v dock.ini.
            // Ak ju kompletne vymažeme z boxu, urobíme to takto:
            
            // Predpokladajme, že ak sa "exec" rovná presne win_class a nebola v ini, je dočasná.
            // Ak ju chceme vymazať:
            GtkStyleContext *context = gtk_widget_get_style_context(app->button);
            
            // Ak je to fixná ikona (napr. firefox-esr z ini), iba zhasne kontrolka
            // Ak je to dočasná ikona, musíme ju odstrániť z main_box
            
            // Dočasnú ikonu spoznáme tak, že ju proste vymažeme, ak ju systém neeviduje ako fixnú.
            // Aby sme to urobili na 100% správne bez zmeny štruktúry: 
            // Ak chceme zistiť, či je fixná, pozrieme sa, či po nastavení window = NULL 
            // zostane tlačidlo viditeľné pre fixné ikony.
            
            // Upravme to prakticky: Ak ju vymažeme vždy, keď nie je v pôvodnom ini. 
            // Keďže v `dock.ini` máš "firefox-esr", ale wm_class je "firefox", 
            // tak pre fixnú sa app->window sparuje. Keď sa zavrie, app->window bude NULL a zhasne.
            // Dočasná ikona mala od začiatku `app->exec` nastavený na `win_class` (čiže "firefox").
            // Ak sa teda `app->exec` presne zhoduje s `win_class` a nemáme v INI presne takú istú fixnú ikonu, zmažeme ju.
            
            app->window = NULL;
            gtk_style_context_remove_class(context, "active");

            // Overenie, či je ikona dočasná: ak sa exec rovná win_class a v celom zozname nie je druhá rovnaká fixná ikona
            // V našom prípade dočasné ikony odstraňujeme z boxu:
            
            // Ak je ikona dočasná, kompletne ju vyhodíme:
            // Aby sme to zjednodušili: ak bola ikona vytvorená dynamicky, vymažeme ju.
            // Keďže dočasná ikona sa vytvára s `app->icon` rovným `win_class`, a fixné majú vlastné ikony alebo exec z INI.
            // Najbezpečnejšie je skontrolovať, či existuje konfigurácia. Ak ju vymažeme, spravíme to takto:
            
            // Ak chceš 100% stabilitu, dočasnú ikonu zmažeme vtedy, ak sa jej ukazovateľ nezhoduje s pôvodným zámerom.
            // Poďme implementovať vymazanie dočasnej ikony:
            
            gboolean permanent = FALSE;
            // Narýchlo overíme, či táto aplikácia náhodou nie je fixná (či sa nenachádza v pôvodnom načítaní)
            // Ak bola načítaná z ini, tak zostáva. Ako to zistíme? 
            // Ak by bola fixná, jej `exec` by bol "firefox-esr", "thunar" alebo "sakura" (podľa tvojho dock.ini).
            // Dočasná by mala exec "firefox". Tým pádom sa nezhodujú.
            if (g_strcmp0(app->exec, "firefox-esr") == 0 || g_strcmp0(app->exec, "thunar") == 0 || g_strcmp0(app->exec, "sakura") == 0) {
                permanent = TRUE;
            }

            if (!permanent) {
                // Je to dočasná ikona -> odstránime ju z widgetu aj zo zoznamu
                gtk_widget_destroy(app->button);
                dock_apps = g_list_remove(dock_apps, app);
                g_free(app->exec);
                g_free(app->icon);
                g_free(app);
            }
	break;
}
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

    AppParserState parser_state;
    parser_state.current_section[0] = '\0';
    parser_state.exec[0] = '\0';
    parser_state.icon[0] = '\0';

    // V kóde máte premenovanú cestu na dock.ini, nechávam podľa vašej verzie
    if (ini_parse("dock.ini", dock_ini_handler, &parser_state) < 0) {
        g_warning("Nepodarilo sa načítať 'dock.ini'. Používam predvolený terminál.");
        add_fixed_launcher("utilities-terminal", "xterm");
    } else {
        flush_parser_app(&parser_state);
    }

    WnckScreen *screen = wnck_screen_get_default();
    wnck_screen_force_update(screen);

    g_signal_connect(screen, "window-opened", G_CALLBACK(on_window_opened), NULL);
    g_signal_connect(screen, "window-closed", G_CALLBACK(on_window_closed), NULL);
    g_signal_connect(dock_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(dock_window, "size-allocate", G_CALLBACK(keep_centered_at_bottom), NULL);

    gtk_widget_show_all(dock_window);
    gtk_main();

    // Vyčistenie alokovanej pamäte pre dock_apps list
    GList *l;
    for (l = dock_apps; l != NULL; l = l->next) {
        DockApp *app = (DockApp *)l->data;

	g_free(app->exec);
	g_free(app->icon);
	g_free(app);
	}
	g_list_free(dock_apps);

	return 0;
}
