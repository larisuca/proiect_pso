#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define XML_FILE "logs.xml"

// Variabile globale
GtkWidget *text_view;
GtkWidget *window;
GtkWidget *btn_info, *btn_warn, *btn_error;

char current_filter[32] = "";
int log_count = 0;

// Contoare pentru statistici
int info_count = 0, warn_count = 0, error_count = 0;

// Cache pentru optimizare
static long last_file_position = 0;
static long initial_file_position = 0;  // ← NOU: Poziția de start
static char previous_filter[32] = "";
static time_t last_file_mtime = 0;
void filter_logs(const char *keyword) {
    struct stat st;
    if (stat(XML_FILE, &st) != 0) return;
    
    FILE *f = fopen(XML_FILE, "r");
    if (!f) return;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    
    // Verifică dacă e prima rulare (last_file_mtime == 0)
    int is_first_run = (last_file_mtime == 0);
    
    // Verifică dacă filtrul s-a schimbat
    if (strcmp(keyword, previous_filter) != 0) {
        // Filtru NOU - curăță buffer-ul
        gtk_text_buffer_set_text(buffer, "", -1);
        log_count = 0;
        
        // ✅ NU reseta last_file_position - păstrează poziția pentru loguri noi!
        // Citește de la poziția inițială (nu de la 0)
        if (initial_file_position > 0) {
            fseek(f, initial_file_position, SEEK_SET);  // ← Începe de la poziția inițială
        } else {
            fseek(f, 0, SEEK_SET);  // ← Dacă nu există poziție inițială, citește tot
        }
        strcpy(previous_filter, keyword);
    } else if (is_first_run || st.st_mtime != last_file_mtime) {
        // Prima rulare SAU fișierul s-a modificat - citim de unde am rămas
        fseek(f, last_file_position, SEEK_SET);
    } else {
        // Același filtru și fișierul nu s-a modificat - nu facem nimic
        fclose(f);
        return;
    }
    
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(buffer, &iter);

    char line[1024];
    char app[64] = "", level[32] = "", msg[256] = "", timestamp[64] = "";

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, " <timestamp>%63[^<]</timestamp>", timestamp) == 1) continue;
        if (sscanf(line, " <app>%63[^<]</app>", app) == 1) continue;
        if (sscanf(line, " <level>%31[^<]</level>", level) == 1) continue;
        if (sscanf(line, " <message>%255[^<]</message>", msg) == 1) {
            if (strstr(level, keyword) || strstr(app, keyword) || strstr(msg, keyword) || strlen(keyword) == 0) {
                char formatted[512];
                snprintf(formatted, sizeof(formatted), "[%s] [%s] [%s]: %s\n",
                         timestamp, app, level, msg);

                GtkTextTag *tag = NULL;
                if (g_strcmp0(level, "INFO") == 0)
                    tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "info_tag");
                else if (g_strcmp0(level, "WARN") == 0)
                    tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "warn_tag");
                else if (g_strcmp0(level, "ERROR") == 0)
                    tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "error_tag");

                if (tag)
                    gtk_text_buffer_insert_with_tags(buffer, &iter, formatted, -1, tag, NULL);
                else
                    gtk_text_buffer_insert(buffer, &iter, formatted, -1);

                log_count++;
            }
            app[0] = level[0] = msg[0] = timestamp[0] = '\0';
        }
    }

    last_file_position = ftell(f);
    last_file_mtime = st.st_mtime;
    fclose(f);

    // Renumără DOAR logurile noi (după poziția inițială)
    f = fopen(XML_FILE, "r"); 
    if (f) {
        info_count = 0;
        warn_count = 0;
        error_count = 0;
        
        char temp_line[1024], temp_level[32];
        long current_pos = 0;
        
        while (fgets(temp_line, sizeof(temp_line), f)) {
            current_pos = ftell(f);
            
            if (sscanf(temp_line, " <level>%31[^<]</level>", temp_level) == 1) {
                // Numără doar dacă suntem DUPĂ poziția inițială
                if (current_pos > initial_file_position) {
                    if (strcmp(temp_level, "INFO") == 0) info_count++;
                    else if (strcmp(temp_level, "WARN") == 0) warn_count++;
                    else if (strcmp(temp_level, "ERROR") == 0) error_count++;
                }
            }
        }
        fclose(f);
        
        // Actualizează label-urile butoanelor
        char btn_label[64];
        snprintf(btn_label, sizeof(btn_label), "INFO (%d)", info_count);
        gtk_button_set_label(GTK_BUTTON(btn_info), btn_label);
        
        snprintf(btn_label, sizeof(btn_label), "WARN (%d)", warn_count);
        gtk_button_set_label(GTK_BUTTON(btn_warn), btn_label);
        
        snprintf(btn_label, sizeof(btn_label), "ERROR (%d)", error_count);
        gtk_button_set_label(GTK_BUTTON(btn_error), btn_label);
    }

    char title[100];
    snprintf(title, sizeof(title), "Remote Log Viewer — %d loguri afișate", log_count);
    gtk_window_set_title(GTK_WINDOW(window), title);
}
// Inițializează poziția la sfârșitul fișierului (pentru a citi doar loguri noi)
void init_file_position() {
    struct stat st;
    if (stat(XML_FILE, &st) == 0) {
        last_file_position = st.st_size;
        initial_file_position = st.st_size;  // ← SALVEAZĂ poziția inițială
        last_file_mtime = st.st_mtime;
    }
}

void on_button_clicked(GtkWidget *widget, gpointer data) {
    strcpy(current_filter, (const char *)data);
    filter_logs(current_filter);
}

void on_search(GtkWidget *entry, gpointer data) {
    strcpy(current_filter, gtk_entry_get_text(GTK_ENTRY(entry)));
    filter_logs(current_filter);
}

gboolean auto_refresh(gpointer data) {
    filter_logs(current_filter);
    return TRUE;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *vbox, *hbox, *scroll, *entry;

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Remote Log Viewer");
    gtk_window_set_default_size(GTK_WINDOW(window), 700, 500);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);

    btn_info = gtk_button_new_with_label("INFO");
    btn_warn = gtk_button_new_with_label("WARN");
    btn_error = gtk_button_new_with_label("ERROR");
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Caută în loguri...");

    gtk_box_pack_start(GTK_BOX(hbox), btn_info, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), btn_warn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), btn_error, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 5);

    text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);

    // Taguri cu culori
    GtkTextTag *info_tag = gtk_text_tag_new("info_tag");
    g_object_set(info_tag, "foreground", "green", NULL);
    gtk_text_tag_table_add(table, info_tag);

    GtkTextTag *warn_tag = gtk_text_tag_new("warn_tag");
    g_object_set(warn_tag, "foreground", "orange", NULL);
    gtk_text_tag_table_add(table, warn_tag);

    GtkTextTag *error_tag = gtk_text_tag_new("error_tag");
    g_object_set(error_tag, "foreground", "red", NULL);
    gtk_text_tag_table_add(table, error_tag);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), text_view);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 5);

    g_signal_connect(btn_info, "clicked", G_CALLBACK(on_button_clicked), "INFO");
    g_signal_connect(btn_warn, "clicked", G_CALLBACK(on_button_clicked), "WARN");
    g_signal_connect(btn_error, "clicked", G_CALLBACK(on_button_clicked), "ERROR");
    g_signal_connect(entry, "activate", G_CALLBACK(on_search), NULL);

    g_timeout_add_seconds(2, auto_refresh, NULL);
    init_file_position();
    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}