//
//                    _)
//   __ `__ \    _` |  |  __ \       __|
//   |   |   |  (   |  |  |   |     (
//  _|  _|  _| \__,_| _| _|  _| _| \___|
//
//

#include "constants.h"
#include "playback.h"
#include "utils.h"
#include "lamp.h"


const GActionEntry entries[] = {
    {"add_file",       addFile,       NULL, NULL, NULL, {0, 0, 0}},
    {"open_folder",    openFolder,    NULL, NULL, NULL, {0, 0, 0}},
    {"open_playlist",  openPlaylist,  NULL, NULL, NULL, {0, 0, 0}},
    {"save_playlist",  savePlaylist,  NULL, NULL, NULL, {0, 0, 0}},
    {"clear_playlist", playlistClear, NULL, NULL, NULL, {0, 0, 0}}
};

int main(int argc, char *argv[]) {
    int status;
    GtkApplication *app = gtk_application_new("gtk.org.lamp", G_APPLICATION_HANDLES_OPEN);
    AppState *state = appStateCreate((GApplication *) app);
    state->widgets = appWidgetsCreate();
    SDL_Init(SDL_INIT_AUDIO | SDL_INIT_EVENTS);
    g_signal_connect(app, "open", G_CALLBACK(appOpen), state);
    g_signal_connect(app, "activate", G_CALLBACK(appActivate), state);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}

/*********************************************************
 * GTK Application Functions
*********************************************************/

void activate(GApplication *app, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    setSizes(state);
    windowInit(state);
    setupUI(state);
    configureUI(state);
    gtk_window_present(GTK_WINDOW(state->widgets->win));
}

void appActivate(GApplication *app, gpointer user_data) {
    activate(app, user_data);
}

void appOpen(GApplication *application, GFile **files, gint n_files, const gchar *hint, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    AppWidgets *widgets = state->widgets;
    activate(application, state);
    for (int i = 0; i < n_files; ++i) {
        playlistAddFile(widgets->playlistStore, files[i], state);
    }
    if (g_list_model_get_n_items(G_LIST_MODEL(widgets->playlistStore))) {
        playlistitemActivate(GTK_LIST_VIEW(widgets->playlist), 0, state);
    }
}

AppWidgets *appWidgetsCreate() {
    AppWidgets *widgets = malloc(sizeof(AppWidgets));
    memset(widgets, 0, sizeof(AppWidgets));
    widgets->playlistStore = g_list_store_new(PLAYLIST_ITEM_TYPE);
    return widgets;
}

AppState *appStateCreate(GApplication *app) {
    AppState *state = malloc(sizeof(AppState));
    memset(state, 0, sizeof(AppState));
    state->settings = malloc(sizeof(Settings));
    memset(state->settings, 0, sizeof(Settings));
    state->app = app;
    state->volume = DEFAULT_VOLUME;
    state->width = WINDOW_WIDTH;
    state->height = WINDOW_HEIGHT;
    state->art_size = ALBUM_ART_WIDTH;
    state->audioFilter = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Audio Files");
    for (int i = 0; i < supportedMimeTypesCount; i++) {
        gtk_file_filter_add_mime_type(filter, supportedMimeTypes[i]);
    }
    g_list_store_append(state->audioFilter, filter);
    return state;
}

void configureUI(AppState *state) {
    AppWidgets *widgets = state->widgets;
    GtkEventControllerKey *key_event = (GtkEventControllerKey *) gtk_event_controller_key_new();
    gtk_widget_add_controller(state->widgets->win, key_event);
    gtk_range_set_value(GTK_RANGE(widgets->volumeSld), DEFAULT_VOLUME);
    g_signal_connect(key_event, "key-pressed", G_CALLBACK(keypressHandler), state);
    g_signal_connect(widgets->volumeSld, "value-changed", G_CALLBACK(volumeChangedCb), state);
    g_signal_connect(widgets->positionSld, "change-value", G_CALLBACK(positionChangedCb), state);
    g_signal_connect(widgets->playBtn, "clicked", G_CALLBACK(playBtnCB), state);
    g_signal_connect(widgets->stopBtn, "clicked", G_CALLBACK(stopBtnCB), state);
    g_signal_connect(widgets->pauseBtn, "clicked", G_CALLBACK(pauseBtnCB), state);
    g_signal_connect(widgets->nextBtn, "clicked", G_CALLBACK(nextBtnCB), state);
    g_signal_connect(widgets->prevBtn, "clicked", G_CALLBACK(prevBtnCB), state);
    g_signal_connect(widgets->repeatTgl, "toggled", G_CALLBACK(repeatTglCB), state);
    g_signal_connect(widgets->shuffleTgl, "toggled", G_CALLBACK(shuffleTglCB), state);
    g_signal_connect(widgets->positionLblGestureClick, "pressed", G_CALLBACK(timeLblPressed), state);
}

GtkWidget *windowInit(AppState *state) {
    GtkWidget *win = gtk_application_window_new((GtkApplication *) state->app);
    gtk_window_set_title(GTK_WINDOW(win), "LAMP");
    gtk_window_set_default_size(GTK_WINDOW(win), state->width, state->height);
    GtkCssProvider *style = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(style, "/res/style.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(style),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_action_map_add_action_entries(G_ACTION_MAP((GtkApplication *) state->app), entries, G_N_ELEMENTS(entries), state);
    state->widgets->win = win;
    return win;
}

// Periodically called function to update state.
// It would be called repeatedly until returns FALSE.
gboolean stateObserver(gpointer user_data) {
    AppState *state = (AppState *) user_data;
    if (!state->playing) { return FALSE; }
    if (state->finished) {
        playNext(state, FALSE, FALSE);
        return FALSE;
    }
    if (state->paused) { return TRUE; }
    setPositionStr(state);
    return TRUE;
}

gboolean keypressHandler(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) {
    AppState *app_state = (AppState *) user_data;
    GtkBitset *selected = NULL;
    GtkBitsetIter iter;
    guint val;
    if (keyval == GDK_KEY_Delete) {
        selected = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(app_state->widgets->selectionModel));
        gtk_bitset_iter_init_first(&iter, selected, &val);
        for (; gtk_bitset_iter_is_valid(&iter); gtk_bitset_iter_next(&iter, &val)) {
            g_list_store_remove(app_state->widgets->playlistStore, val);
        }
    }
    return TRUE;
}

/*********************************************************
 * Playback Functions
*********************************************************/

void playlistitemActivateCb(GtkListView *list, guint position, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    playlistitemActivate(list, position, state);
}

void playlistitemActivate(GtkListView *list, guint position, AppState *state) {
    PlaylistItem *item = g_list_model_get_item(G_LIST_MODEL(gtk_list_view_get_model(list)), position);
    if (!item) { return; }
    unHighlightRow(state, state->current_index);
    state->current_index = (int) position;
    state->current_file = item->filePath;
    if (state->audio_ctx != NULL) playThreadStop(state);
    highlightRow(state, position);
    playThreadCreate(state);
    g_timeout_add(UI_UPDATE_INTERVAL, stateObserver, state);
    g_object_unref(item);
}

void playThreadCreate(AppState *state) {
    play(state);
    updateUI(state);
}

void playThreadStop(AppState *state) {
    stopAudio(state);
    resetUI(state);
}

void play(AppState *state) {
    if (!state->current_file) {
        printf("ERROR: No file is specified for play.\n");
        return;
    }
    if ((playAudio(state, state->current_file)) != 0) {
        printf("ERROR: Can't start playing audio file %s.\n", state->current_file);
    }
}

void volumeChangedCb(GtkRange *self, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    state->volume = (int) gtk_range_get_value(self);
}

gboolean positionChangedCb(GtkRange *self, GtkScrollType *scroll, gdouble value, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    if (!state->audio_ctx) { return TRUE; }
    packetQueueSeek(state, value);
    return FALSE;
}

void playBtnCB(GtkButton *self, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    if (state->paused) {
        state->paused = FALSE;
        SDL_PauseAudioDevice(state->audio_ctx->audio_device, state->paused);
    } else {
        playlistitemActivate(GTK_LIST_VIEW(state->widgets->playlist), state->current_index, state);
    }
}

void stopBtnCB(GtkButton *self, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    playThreadStop(state);
}

void pauseBtnCB(GtkButton *self, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    if (!state->audio_ctx) { return; }
    state->paused = !state->paused;
    SDL_PauseAudioDevice(state->audio_ctx->audio_device, state->paused);
}

void playNext(AppState *state, gboolean backward, gboolean force) {
    double position, page_size, item_position, item_size;
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->widgets->playlistScr));
    guint items_count = g_list_model_get_n_items(G_LIST_MODEL(state->widgets->playlistStore));
    unHighlightRow(state, state->current_index);
    gboolean exceeds = setNextItem(state, backward, force);
    if (force) {
        gtk_selection_model_select_item(GTK_SELECTION_MODEL(state->widgets->selectionModel), state->current_index, TRUE);
        position = gtk_adjustment_get_value(adj);
        page_size = gtk_adjustment_get_page_size(adj);
        item_size = gtk_adjustment_get_upper(adj) / items_count;
        item_position = item_size * state->current_index;
        if (!isBetween(item_position, position, position + page_size - item_size)) {
            gtk_adjustment_set_value(adj, item_position - item_size);
        }
    }
    if (exceeds && state->repeat != REPEAT_ALL) {
        playThreadStop(state);
        return;
    }
    if (!state->playing) { return; }
    playlistitemActivate(GTK_LIST_VIEW(state->widgets->playlist), state->current_index, state);
}

void nextBtnCB(GtkButton *self, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    playNext(state, FALSE, TRUE);
}

void prevBtnCB(GtkButton *self, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    playNext(state, TRUE, TRUE);
}

// Returns TRUE if exceeds the playlist items count, else FALSE
int setNextItem(AppState *state, gboolean backwards, gboolean force) {
    int n_items = (int) g_list_model_get_n_items(G_LIST_MODEL(state->widgets->playlistStore));
    int step = backwards ? -1 : 1;
    int *current = NULL;
    gboolean exceed = FALSE;
    if (!force && state->repeat == REPEAT_ONE) {
        return exceed;
    }
    current = state->shuffle_arr ? &state->shuffle_current : &state->current_index;
    *current += step;
    if (*current >= n_items) {
        *current = 0;
        exceed = TRUE;
    } else if (*current < 0) {
        *current = n_items - 1;
        exceed = TRUE;
    }
    if (state->shuffle_arr) {
        state->current_index = state->shuffle_arr[*current];
    }
    return exceed;
}

void repeatTglCB(GtkToggleButton *self, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    switch (state->repeat) {
        case REPEAT_OFF: {
            state->repeat = REPEAT_ALL;
            break;
        }
        case REPEAT_ALL: {
            gtk_image_clear(GTK_IMAGE(state->widgets->repeatImg));
            gtk_image_set_from_icon_name(GTK_IMAGE(state->widgets->repeatImg), "media-playlist-repeat-song-symbolic");
            gtk_toggle_button_set_active(self, TRUE);
            state->repeat = REPEAT_ONE;
            break;
        }
        case REPEAT_ONE: {
            gtk_image_clear(GTK_IMAGE(state->widgets->repeatImg));
            gtk_image_set_from_icon_name(GTK_IMAGE(state->widgets->repeatImg), "media-playlist-repeat-symbolic");
            state->repeat = REPEAT_OFF;
        }
        default: {
            break;
        }
    }
}

void shuffleTglCB(GtkToggleButton *self, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    clearShuffle(state);
    if (gtk_toggle_button_get_active(self)) {
        shufflePlaylist(state);
    }
}

static void shufflePlaylist(AppState *state) {
    guint num1, num2, tmp;
    srand(time(0));
    guint n_items = g_list_model_get_n_items(G_LIST_MODEL(state->widgets->playlistStore));
    state->shuffle_arr = malloc(sizeof(int) * n_items);
    for (int i = 0; i < n_items; i++) { state->shuffle_arr[i] = i; }
    for (int i = 0; i < n_items / 2; i++) {
        num1 = rand() % n_items;
        num2 = rand() % n_items;
        tmp = state->shuffle_arr[num1];
        state->shuffle_arr[num1] = state->shuffle_arr[num2];
        state->shuffle_arr[num2] = tmp;
    }
}

static void clearShuffle(AppState *state) {
    if (state->shuffle_arr) { free(state->shuffle_arr); }
    state->shuffle_arr = NULL;
    state->shuffle_current = 0;
}

/*********************************************************
 * Playlist Functions
*********************************************************/

void addFile(GSimpleAction *action, GVariant *parameter, gpointer data) {
    AppState *state = (AppState *) data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open File(s)");
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(state->audioFilter));
    gtk_file_dialog_open_multiple(dialog, GTK_WINDOW(state->widgets->win), NULL, onFileAddCb, state);
}

void openFolder(GSimpleAction *action, GVariant *parameter, gpointer data) {
    AppState *state = (AppState *) data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open Folder");
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(state->widgets->win), NULL, onFolderAddCb, state);
}

void openPlaylist(GSimpleAction *action, GVariant *parameter, gpointer data) {
    AppState *state = (AppState *) data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open Playlist");
    gtk_file_dialog_open(dialog, GTK_WINDOW(state->widgets->win), NULL, onPlaylistOpenCb, state);
}

void savePlaylist(GSimpleAction *action, GVariant *parameter, gpointer data) {
    AppState *state = (AppState *) data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save Playlist");
    gtk_file_dialog_set_initial_name(dialog, "Untitled");
    gtk_file_dialog_save(dialog, GTK_WINDOW(state->widgets->win), NULL, onPlaylistSaveCb, state);
}

void playlistClear(GSimpleAction *action, GVariant *parameter, gpointer data) {
    AppState *state = (AppState *) data;
    g_list_store_remove_all(state->widgets->playlistStore);
}

void playlistitemBindCb(GtkListItemFactory *factory, GtkListItem *list_item, gpointer *user_data) {
    AppState *state = (AppState *) user_data;
    GtkWidget *box = gtk_list_item_get_child(list_item);
    GtkWidget *parent = gtk_widget_get_parent(box);
    GtkWidget *label = gtk_widget_get_first_child(box);
    GtkWidget *filepath = gtk_widget_get_next_sibling(label);
    PlaylistItem *item = gtk_list_item_get_item(list_item);
    gtk_label_set_label(GTK_LABEL(label), item->name);
    gtk_label_set_label(GTK_LABEL(filepath), item->filePath);
    // set list item, so there is always one to push off from.
    state->widgets->listItem = list_item;
    if (state->playing && state->current_index == item->index) {
        gtk_widget_add_css_class(parent, "highlight-row");
    } else {
        gtk_widget_remove_css_class(parent, "highlight-row");
    }
}

void playlistitemUnbindCb(GtkListItemFactory *factory, GtkListItem *list_item, gpointer *user_data) {
    AppState *state = (AppState *) user_data;
    if (state->widgets->listItem == list_item) {
        state->widgets->listItem = NULL;
    }
}

void onFileAddCb(GtkFileDialog *self, GAsyncResult *res, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    gpointer item;
    GListModel *list = gtk_file_dialog_open_multiple_finish(self, res, NULL);
    for (int i = 0; (item = g_list_model_get_item(list, i)); i++) {
        playlistAddFile(state->widgets->playlistStore, item, state);
    }
    g_object_unref(list);
    g_object_unref(self);
}

void onFolderAddCb(GtkFileDialog *self, GAsyncResult *res, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    GFile *folder = gtk_file_dialog_select_folder_finish(self, res, NULL);
    GFileInfo *info;
    GFileEnumerator *iter = g_file_enumerate_children(
        folder, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
        G_FILE_QUERY_INFO_NONE, NULL, NULL
    );
    while ((info = g_file_enumerator_next_file(iter, NULL, NULL))) {
        GFile *file = g_file_new_build_filename(g_file_get_path(folder), g_file_info_get_name(info), NULL);
        if (strInArray(g_file_info_get_content_type(info), (char **) supportedMimeTypes, supportedMimeTypesCount)) {
            playlistAddFile(state->widgets->playlistStore, file, state);
        }
        g_object_unref(file);
    }
    g_object_unref(folder);
    g_object_unref(iter);
    g_object_unref(self);
}

void onPlaylistOpenCb(GtkFileDialog *self, GAsyncResult *res, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    GFile *file = gtk_file_dialog_open_finish(self, res, NULL);
    if (!file) { return; }
    playlistClear(NULL, NULL, state);
    playlistLoad(g_file_get_path(file), state);
    g_object_unref(file);
    g_object_unref(self);
}

void onPlaylistSaveCb(GtkFileDialog *self, GAsyncResult *res, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    GFile *file = gtk_file_dialog_save_finish(self, res, NULL);
    if (!file) { return; }
    playlistSave(g_file_get_path(file), state);
    g_object_unref(self);
}

void playlistAddFile(GListStore *playlist, GFile *file, AppState *state) {
    PlaylistItem *item = g_object_new(PLAYLIST_ITEM_TYPE, NULL);
    strcpy(item->filePath, g_file_get_path(file));
    strcpy(item->name, g_file_get_basename(file));
    item->index = state->index_counter++;
    g_list_store_append(playlist, item);
}

void playlistLoad(const char *filename, AppState *state) {
    gchar *line = malloc(1024 * sizeof(char));
    FILE *fp = fopen(filename, "r");
    while (fgets(line, 1024, fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        PlaylistItem *item = g_object_new(PLAYLIST_ITEM_TYPE, NULL);
        strcpy(item->filePath, line);
        strcpy(item->name, g_path_get_basename(line));
        g_list_store_append(state->widgets->playlistStore, item);
    }
    g_free(line);
}

void playlistSave(const char *filename, AppState *state) {
    PlaylistItem *item;
    FILE *f = fopen(filename, "w");
    for (int i = 0; (item = g_list_model_get_item(G_LIST_MODEL(state->widgets->playlistStore), i)); i++) {
        fprintf(f, "%s\n", item->filePath);
    }
    fclose(f);
}

/*********************************************************
 * User Interface Functions
*********************************************************/

void setSizes(AppState *state) {
    GdkRectangle r;
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = g_list_model_get_item(gdk_display_get_monitors(display), 0);
    gdk_monitor_get_geometry(monitor, &r);
    if (r.width > 2500) {
        state->width = 540;
        state->height = 900;
        state->art_size = 180;
    } else if (r.width > 1800) {
        state->width = 480;
        state->height = 760;
        state->art_size = 144;
    } else if (r.width > 1300) {
        state->width = 400;
        state->height = 540;
        state->art_size = 128;
    }
}

void resetUI(AppState *state) {
    AppWidgets *widgets = state->widgets;
    gtk_range_set_range(GTK_RANGE(widgets->positionSld), 0, 1);
    gtk_range_set_value(GTK_RANGE(widgets->positionSld), 0);
    gtk_widget_set_sensitive(widgets->positionSld, FALSE);
    gtk_label_set_text(GTK_LABEL(widgets->titleLbl), "");
    gtk_label_set_text(GTK_LABEL(widgets->albumLbl), "");
    gtk_label_set_text(GTK_LABEL(widgets->artistLbl), "");
    gtk_label_set_text(GTK_LABEL(widgets->durationLbl), "");
    gtk_label_set_text(GTK_LABEL(widgets->positionLbl), "");
    setDefaultImage(widgets->coverImg);
    unHighlightRow(state, state->current_index);
}

void updateUI(AppState *state) {
    AppWidgets *widgets = state->widgets;
    AVDictionaryEntry *tag = NULL;

    if (state->audio_ctx->duration) {
        gtk_range_set_range(GTK_RANGE(widgets->positionSld), 0, state->audio_ctx->duration);
        gtk_widget_set_sensitive(widgets->positionSld, TRUE);
    }
    char *duration_str = secondsToStr((int) state->audio_ctx->duration, FALSE);
    gtk_label_set_text(GTK_LABEL(widgets->durationLbl), duration_str);
    free(duration_str);

    if ((tag = av_dict_get(state->audio_ctx->format_ctx->metadata, "title", NULL, AV_DICT_IGNORE_SUFFIX))) {
        gtk_label_set_text(GTK_LABEL(widgets->titleLbl), tag->value);
    }
    if ((tag = av_dict_get(state->audio_ctx->format_ctx->metadata, "album", NULL, AV_DICT_IGNORE_SUFFIX))) {
        gtk_label_set_text(GTK_LABEL(widgets->albumLbl), tag->value);
    }
    if ((tag = av_dict_get(state->audio_ctx->format_ctx->metadata, "artist", NULL, AV_DICT_IGNORE_SUFFIX))) {
        gtk_label_set_text(GTK_LABEL(widgets->artistLbl), tag->value);
    }
    gtk_label_set_text(GTK_LABEL(state->widgets->positionLbl), "0:00");
    setPictureToWidget(state->current_file, state->widgets->coverImg, ALBUM_ART_WIDTH, ALBUM_ART_WIDTH);
}

void setDefaultImage(GtkWidget *image) {
    gtk_image_set_from_resource(GTK_IMAGE(image), "/res/img/logo.jpg");
}

void setPositionStr(AppState *state) {
    guint position = state->settings->time_remain ? state->audio_ctx->duration - state->audio_ctx->position : state->audio_ctx->position;
    char *position_str = secondsToStr((int) position, state->settings->time_remain);
    gtk_range_set_value(GTK_RANGE(state->widgets->positionSld), state->audio_ctx->position);
    gtk_label_set_text(GTK_LABEL(state->widgets->positionLbl), position_str);
    free(position_str);
}

void timeLblPressed(GtkGestureClick *self, gint n_press, gdouble x, gdouble y, gpointer user_data) {
    AppState *state = (AppState *) user_data;
    state->settings->time_remain = !state->settings->time_remain;
}

int setPictureToWidget(const char *filename, GtkWidget *widget, int width, int height) {
    GdkPixbufLoader *loader = NULL;
    GdkPixbuf *pix_buf = NULL;
    AVPacket *album_art = NULL;
    album_art = getAlbumArt(filename);
    if (album_art) {
        loader = gdk_pixbuf_loader_new();
        gdk_pixbuf_loader_write(loader, album_art->data, album_art->size, NULL);
        pix_buf = gdk_pixbuf_loader_get_pixbuf(loader);
        gtk_image_set_from_pixbuf(GTK_IMAGE(widget), pix_buf);
        gdk_pixbuf_loader_close(loader, NULL);
        g_object_unref(loader);
        av_packet_free(&album_art);
        return 0;
    }
    return 1;
}

GtkWidget *getRowAtPosition(AppState *state, guint position) {
    if (!state->widgets->listItem) { return NULL; }
    gboolean reverse = FALSE;
    guint current = gtk_list_item_get_position(state->widgets->listItem);
    GtkWidget *item = gtk_widget_get_parent(gtk_list_item_get_child(state->widgets->listItem));
    reverse = position > current ? FALSE : TRUE;
    while (item) {
        if (current == position) {
            return item;
        } else if (reverse) {
            item = gtk_widget_get_prev_sibling(item);
            current--;
        } else {
            item = gtk_widget_get_next_sibling(item);
            current++;
        }
    }
    return NULL;
}

void highlightRow(AppState *state, guint position) {
    GtkWidget *item = NULL;
    if ((item = getRowAtPosition(state, position))) {
        gtk_widget_add_css_class(item, "highlight-row");
    }
}

void unHighlightRow(AppState *state, guint position) {
    GtkWidget *item = NULL;
    if ((item = getRowAtPosition(state, position))) {
        gtk_widget_remove_css_class(item, "highlight-row");
    }
}

void playlistitemSetupCb(GtkListItemFactory *factory, GtkListItem *list_item, gpointer *user_data) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *label = gtk_label_new(NULL);
    GtkWidget *filepath = gtk_label_new(NULL);
    PangoAttrList *nameAttrs = pango_attr_list_new();
    PangoAttrList *pathAttrs = pango_attr_list_new();
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_xalign(GTK_LABEL(filepath), 0);
    pango_attr_list_insert(nameAttrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(nameAttrs, pango_attr_size_new(15 * PANGO_SCALE));
    pango_attr_list_insert(pathAttrs, pango_attr_size_new(13 * PANGO_SCALE));
    pango_attr_list_insert(nameAttrs, pango_attr_family_new("Open Sans"));
    gtk_label_set_attributes(GTK_LABEL(label), nameAttrs);
    gtk_label_set_attributes(GTK_LABEL(filepath), pathAttrs);
    pango_attr_list_unref(nameAttrs);
    gtk_widget_set_margin_start(label, 20);
    gtk_widget_set_margin_end(label, 16);
    gtk_widget_set_margin_top(label, 24);
    gtk_widget_set_margin_start(filepath, 20);
    gtk_widget_set_margin_end(filepath, 16);
    gtk_widget_set_margin_bottom(filepath, 24);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_ellipsize(GTK_LABEL(filepath), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), filepath);
    gtk_list_item_set_child(list_item, box);
}

void setupUI(AppState *state) {
    AppWidgets *widgets = state->widgets;
    widgets->headerBar = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(widgets->win), widgets->headerBar);
    GtkWidget *mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(widgets->win), mainBox);
    /* Header Box */
    GtkWidget *headerBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_valign(headerBox, GTK_ALIGN_START);
    gtk_widget_set_margin_top(headerBox, 8);
    gtk_widget_set_margin_start(headerBox, 12);
    gtk_widget_set_margin_end(headerBox, 12);
    gtk_box_append(GTK_BOX(mainBox), headerBox);
    widgets->coverImg = gtk_image_new();
    gtk_widget_set_size_request(widgets->coverImg, state->art_size, state->art_size);
    setDefaultImage(widgets->coverImg);
    gtk_box_append(GTK_BOX(headerBox), widgets->coverImg);
    GtkWidget *infoGrid = gtk_grid_new();
    gtk_grid_set_row_homogeneous(GTK_GRID(infoGrid), TRUE);
    widgets->titleLbl = gtk_label_new(NULL);
    widgets->albumLbl = gtk_label_new(NULL);
    widgets->artistLbl = gtk_label_new(NULL);
    widgets->durationLbl = gtk_label_new(NULL);
    PangoAttrList *nameAttrs = pango_attr_list_new();
    pango_attr_list_insert(nameAttrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(nameAttrs, pango_attr_size_new(15 * PANGO_SCALE));
    pango_attr_list_insert(nameAttrs, pango_attr_family_new("Open Sans"));
    gtk_label_set_attributes(GTK_LABEL(widgets->titleLbl), nameAttrs);
    pango_attr_list_unref(nameAttrs);
    gtk_label_set_justify(GTK_LABEL(widgets->titleLbl), GTK_JUSTIFY_LEFT);
    gtk_label_set_justify(GTK_LABEL(widgets->albumLbl), GTK_JUSTIFY_LEFT);
    gtk_label_set_justify(GTK_LABEL(widgets->artistLbl), GTK_JUSTIFY_LEFT);
    gtk_label_set_justify(GTK_LABEL(widgets->durationLbl), GTK_JUSTIFY_LEFT);
    gtk_label_set_xalign(GTK_LABEL(widgets->titleLbl), 0);
    gtk_label_set_xalign(GTK_LABEL(widgets->albumLbl), 0);
    gtk_label_set_xalign(GTK_LABEL(widgets->artistLbl), 0);
    gtk_label_set_xalign(GTK_LABEL(widgets->durationLbl), 0);
    gtk_label_set_ellipsize(GTK_LABEL(widgets->titleLbl), PANGO_ELLIPSIZE_END);
    gtk_label_set_ellipsize(GTK_LABEL(widgets->albumLbl), PANGO_ELLIPSIZE_END);
    gtk_label_set_ellipsize(GTK_LABEL(widgets->artistLbl), PANGO_ELLIPSIZE_END);
    gtk_label_set_ellipsize(GTK_LABEL(widgets->durationLbl), PANGO_ELLIPSIZE_END);
    gtk_grid_attach(GTK_GRID(infoGrid), widgets->titleLbl, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(infoGrid), widgets->albumLbl, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(infoGrid), widgets->artistLbl, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(infoGrid), widgets->durationLbl, 0, 3, 1, 1);
    gtk_widget_set_margin_start(infoGrid, 16);
    gtk_widget_set_margin_top(infoGrid, 4);
    gtk_box_append(GTK_BOX(headerBox), infoGrid);
    /* Pannel Box */
    GtkWidget *pannelBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(pannelBox, 16);
    gtk_widget_set_margin_start(pannelBox, 12);
    gtk_widget_set_margin_end(pannelBox, 12);
    widgets->volumeSld = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, SDL_MIX_MAXVOLUME, 1);
    gtk_range_set_round_digits(GTK_RANGE(widgets->volumeSld), 0);
    gtk_widget_set_hexpand(widgets->volumeSld, TRUE);
    gtk_widget_add_css_class(widgets->volumeSld, "sld-volume");
    gtk_box_append(GTK_BOX(pannelBox), widgets->volumeSld);
    GtkWidget *pannelGrid = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(pannelGrid), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(pannelGrid), 8);
    widgets->repeatTgl = gtk_toggle_button_new();
    widgets->shuffleTgl = gtk_toggle_button_new();
    widgets->settingsBtn = gtk_button_new();
    widgets->repeatImg = gtk_image_new_from_icon_name("media-playlist-repeat-symbolic");
    widgets->shuffleImg = gtk_image_new_from_icon_name("media-playlist-shuffle-symbolic");
    widgets->settingsImg = gtk_image_new_from_icon_name("preferences-system-symbolic");
    gtk_image_set_icon_size(GTK_IMAGE(widgets->repeatImg), GTK_ICON_SIZE_NORMAL);
    gtk_image_set_icon_size(GTK_IMAGE(widgets->shuffleImg), GTK_ICON_SIZE_NORMAL);
    gtk_image_set_icon_size(GTK_IMAGE(widgets->settingsImg), GTK_ICON_SIZE_NORMAL);
    gtk_widget_add_css_class(widgets->repeatImg, "icon-md");
    gtk_widget_add_css_class(widgets->shuffleImg, "icon-md");
    gtk_widget_add_css_class(widgets->settingsImg, "icon-md");
    gtk_widget_add_css_class(widgets->repeatTgl, "flat");
    gtk_widget_add_css_class(widgets->shuffleTgl, "flat");
    gtk_widget_add_css_class(widgets->settingsBtn, "flat");
    gtk_button_set_child(GTK_BUTTON(widgets->repeatTgl), widgets->repeatImg);
    gtk_button_set_child(GTK_BUTTON(widgets->shuffleTgl), widgets->shuffleImg);
    gtk_button_set_child(GTK_BUTTON(widgets->settingsBtn), widgets->settingsImg);
    gtk_grid_attach(GTK_GRID(pannelGrid), widgets->repeatTgl, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(pannelGrid), widgets->shuffleTgl, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(pannelGrid), widgets->settingsBtn, 2, 0, 1, 1);
    gtk_box_append(GTK_BOX(pannelBox), pannelGrid);
    gtk_box_append(GTK_BOX(mainBox), pannelBox);
    /* Position Pannel */
    GtkWidget *positionBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(positionBox, 12);
    gtk_widget_set_margin_start(positionBox, 12);
    gtk_widget_set_margin_end(positionBox, 12);
    widgets->positionSld = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 1);
    gtk_range_set_round_digits(GTK_RANGE(widgets->positionSld), 0);
    gtk_widget_set_sensitive(widgets->positionSld, FALSE);
    gtk_widget_set_hexpand(widgets->positionSld, TRUE);
    gtk_widget_add_css_class(widgets->positionSld, "sld-position");
    widgets->positionLblGestureClick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(widgets->positionLblGestureClick), GDK_BUTTON_PRIMARY);
    widgets->positionLbl = gtk_label_new(NULL);
    gtk_widget_add_controller(widgets->positionLbl, GTK_EVENT_CONTROLLER(widgets->positionLblGestureClick));
    gtk_label_set_width_chars(GTK_LABEL(widgets->positionLbl), 6);
    gtk_label_set_justify(GTK_LABEL(widgets->positionLbl), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(positionBox), widgets->positionSld);
    gtk_box_append(GTK_BOX(positionBox), widgets->positionLbl);
    gtk_box_append(GTK_BOX(mainBox), positionBox);
    /* Control Panel */
    GtkWidget *controlGrid = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(controlGrid), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(controlGrid), 16);
    gtk_widget_set_margin_top(controlGrid, 16);
    gtk_widget_set_margin_start(controlGrid, 12);
    gtk_widget_set_margin_end(controlGrid, 12);
    widgets->prevBtn = gtk_button_new();
    widgets->stopBtn = gtk_button_new();
    widgets->playBtn = gtk_button_new();
    widgets->pauseBtn = gtk_button_new();
    widgets->nextBtn = gtk_button_new();
    gtk_widget_add_css_class(widgets->prevBtn, "flat");
    gtk_widget_add_css_class(widgets->stopBtn, "flat");
    gtk_widget_add_css_class(widgets->playBtn, "flat");
    gtk_widget_add_css_class(widgets->pauseBtn, "flat");
    gtk_widget_add_css_class(widgets->nextBtn, "flat");
    gtk_grid_attach(GTK_GRID(controlGrid), widgets->prevBtn, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(controlGrid), widgets->stopBtn, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(controlGrid), widgets->playBtn, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(controlGrid), widgets->pauseBtn, 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(controlGrid), widgets->nextBtn, 4, 0, 1, 1);
    widgets->prevImg = gtk_image_new_from_icon_name("media-skip-backward-symbolic");
    widgets->stopImg = gtk_image_new_from_icon_name("media-playback-stop-symbolic");
    widgets->playImg = gtk_image_new_from_icon_name("media-playback-start-symbolic");
    widgets->pauseImg = gtk_image_new_from_icon_name("media-playback-pause-symbolic");
    widgets->nextImg = gtk_image_new_from_icon_name("media-skip-forward-symbolic");
    gtk_widget_set_margin_top(widgets->prevImg, 4);
    gtk_widget_set_margin_top(widgets->stopImg, 4);
    gtk_widget_set_margin_top(widgets->playImg, 4);
    gtk_widget_set_margin_top(widgets->pauseImg, 4);
    gtk_widget_set_margin_top(widgets->nextImg, 4);
    gtk_widget_set_margin_bottom(widgets->prevImg, 4);
    gtk_widget_set_margin_bottom(widgets->stopImg, 4);
    gtk_widget_set_margin_bottom(widgets->playImg, 4);
    gtk_widget_set_margin_bottom(widgets->pauseImg, 4);
    gtk_widget_set_margin_bottom(widgets->nextImg, 4);
    gtk_widget_add_css_class(widgets->prevImg, "icon-md");
    gtk_widget_add_css_class(widgets->stopImg, "icon-md");
    gtk_widget_add_css_class(widgets->playImg, "icon-lg");
    gtk_widget_add_css_class(widgets->pauseImg, "icon-md");
    gtk_widget_add_css_class(widgets->nextImg, "icon-md");
    gtk_button_set_child(GTK_BUTTON(widgets->prevBtn), widgets->prevImg);
    gtk_button_set_child(GTK_BUTTON(widgets->stopBtn), widgets->stopImg);
    gtk_button_set_child(GTK_BUTTON(widgets->playBtn), widgets->playImg);
    gtk_button_set_child(GTK_BUTTON(widgets->pauseBtn), widgets->pauseImg);
    gtk_button_set_child(GTK_BUTTON(widgets->nextBtn), widgets->nextImg);
    gtk_box_append(GTK_BOX(mainBox), controlGrid);
    /* Playlist Top Bar */
    GtkWidget *playlistBarBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget *playlistMenuBtn = gtk_menu_button_new();
    widgets->playlistLbl = gtk_label_new("Default");
    gtk_widget_set_margin_top(playlistBarBox, 16);
    gtk_widget_set_margin_start(playlistBarBox, 12);
    gtk_widget_set_margin_end(playlistBarBox, 12);
    gtk_widget_set_margin_bottom(playlistBarBox, 8);
    widgets->menuBtnImg = gtk_image_new_from_icon_name("content-loading-symbolic");
    gtk_menu_button_set_child(GTK_MENU_BUTTON(playlistMenuBtn), widgets->menuBtnImg);
    gtk_widget_add_css_class(playlistMenuBtn, "icon-md");
    gtk_widget_add_css_class(gtk_widget_get_first_child(playlistMenuBtn), "flat");
    gtk_widget_set_margin_top(playlistMenuBtn, 4);
    gtk_widget_set_margin_bottom(playlistMenuBtn, 4);
    /* Playlist Menu */
    GMenu *playlistMenu = g_menu_new();
    GMenuItem *mItemAddFile = g_menu_item_new("Add File(s)", "app.add_file");
    GMenuItem *mItemOpenFolder = g_menu_item_new("Open Folder", "app.open_folder");
    GMenuItem *mItemOpenPlst = g_menu_item_new("Open Playlist", "app.open_playlist");
    GMenuItem *mItemSavePlst = g_menu_item_new("Save Playlist", "app.save_playlist");
    GMenuItem *mItemClearPlst = g_menu_item_new("Clear Playlist", "app.clear_playlist");
    GtkWidget *playlistPop = gtk_popover_menu_new_from_model(G_MENU_MODEL(playlistMenu));
    gtk_widget_set_halign(playlistPop, GTK_ALIGN_START);
    gtk_popover_set_has_arrow(GTK_POPOVER(playlistPop), FALSE);
    g_menu_append_item(playlistMenu, mItemAddFile);
    g_object_unref(mItemAddFile);
    g_menu_append_item(playlistMenu, mItemOpenFolder);
    g_object_unref(mItemOpenFolder);
    g_menu_append_item(playlistMenu, mItemOpenPlst);
    g_object_unref(mItemOpenPlst);
    g_menu_append_item(playlistMenu, mItemSavePlst);
    g_object_unref(mItemSavePlst);
    g_menu_append_item(playlistMenu, mItemClearPlst);
    g_object_unref(mItemClearPlst);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(playlistMenuBtn), playlistPop);
    /* Playlist Label */
    gtk_label_set_ellipsize(GTK_LABEL(widgets->playlistLbl), PANGO_ELLIPSIZE_END);
    gtk_label_set_justify(GTK_LABEL(widgets->playlistLbl), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(widgets->playlistLbl), (float) 0.4);
    gtk_widget_set_hexpand(widgets->playlistLbl, TRUE);
    PangoAttrList *playlistAttrs = pango_attr_list_new();
    pango_attr_list_insert(playlistAttrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(playlistAttrs, pango_attr_size_new(14 * PANGO_SCALE));
    pango_attr_list_insert(playlistAttrs, pango_attr_family_new("Sans Bold"));
    gtk_label_set_attributes(GTK_LABEL(widgets->playlistLbl), playlistAttrs);
    pango_attr_list_unref(playlistAttrs);
    gtk_box_append(GTK_BOX(playlistBarBox), playlistMenuBtn);
    gtk_box_append(GTK_BOX(playlistBarBox), widgets->playlistLbl);
    gtk_box_append(GTK_BOX(mainBox), playlistBarBox);
    /* Playlist */
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(playlistitemSetupCb), state);
    g_signal_connect(factory, "bind", G_CALLBACK(playlistitemBindCb), state);
    g_signal_connect(factory, "unbind", G_CALLBACK(playlistitemUnbindCb), state);
    widgets->playlistScr = gtk_scrolled_window_new();
    widgets->selectionModel = gtk_multi_selection_new(G_LIST_MODEL(widgets->playlistStore));
    widgets->playlist = gtk_list_view_new(GTK_SELECTION_MODEL(widgets->selectionModel), factory);
    g_signal_connect(widgets->playlist, "activate", G_CALLBACK(playlistitemActivateCb), state);
    gtk_widget_set_vexpand(widgets->playlistScr, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(widgets->playlistScr), widgets->playlist);
    gtk_box_append(GTK_BOX(mainBox), widgets->playlistScr);
}
