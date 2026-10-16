/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <config.h>
#include "nautilus-miller-preview.h"
#include "nautilus-file.h"
#include <errno.h>
#include <fcntl.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEXT_INPUT_LIMIT (128 * 1024)
#define TEXT_DISPLAY_LIMIT (32 * 1024)
#define INFO_ATTRIBUTES "standard::display-name,standard::type,standard::size,standard::is-symlink,time::modified,unix::device,unix::inode"

typedef struct
{
    GWeakRef owner;
    guint64 generation;
    GFile *file;
    GFileInfo *info;
    GCancellable *cancellable;
    char *mime;
} PreviewRequest;

struct _NautilusMillerPreview
{
    AdwBin parent_instance;
    GtkLabel *title, *kind, *size, *modified, *status;
    GtkImage *icon;
    GtkTextView *text_view;
    NautilusFileList *files;
    PreviewRequest *request; /* Owned by the outstanding asynchronous chain. */
    guint64 generation, known_size;
    GList *aggregate_next;
    guint aggregate_idle, deadline;
    gboolean active, loading, disposed, queued, timed_out, partial;
};

G_DEFINE_FINAL_TYPE (NautilusMillerPreview, nautilus_miller_preview, ADW_TYPE_BIN)
static GParamSpec *loading_property;
static void refresh (NautilusMillerPreview *self);
static void start_request (NautilusMillerPreview *self);

static gboolean
current (NautilusMillerPreview *self, guint64 generation)
{
    return !self->disposed && self->generation == generation;
}

static gboolean
work_allowed (NautilusMillerPreview *self)
{
    return !self->disposed && self->active && gtk_widget_get_mapped (GTK_WIDGET (self)) &&
           gtk_widget_get_visible (GTK_WIDGET (self));
}

static void
set_loading (NautilusMillerPreview *self, gboolean loading)
{
    if (self->loading != loading)
    {
        self->loading = loading;
        g_object_notify_by_pspec (G_OBJECT (self), loading_property);
    }
}

static gboolean
label_text (NautilusMillerPreview *self, GtkLabel *label, const char *text, guint64 generation)
{
    if (!current (self, generation)) { return FALSE; }
    gtk_label_set_text (label, text);
    return current (self, generation);
}

static void
request_free (PreviewRequest *request)
{
    g_weak_ref_clear (&request->owner);
    g_clear_object (&request->file);
    g_clear_object (&request->info);
    g_clear_object (&request->cancellable);
    g_free (request->mime);
    g_free (request);
}

static void
request_finished (PreviewRequest *request)
{
    g_autoptr (NautilusMillerPreview) self = g_weak_ref_get (&request->owner);
    if (self != NULL && self->request == request)
    {
        self->request = NULL;
        if (current (self, request->generation))
        {
            self->queued = FALSE;
            g_clear_handle_id (&self->deadline, g_source_remove);
            set_loading (self, FALSE);
        }
    }
    request_free (request);
    if (self != NULL && self->queued && !self->timed_out && work_allowed (self)) { start_request (self); }
}

static gboolean
request_timed_out (gpointer data)
{
    g_autoptr (NautilusMillerPreview) self = g_object_ref (data);
    guint64 generation = self->generation;
    self->deadline = 0;
    self->timed_out = TRUE;
    self->queued = FALSE;
    if (self->request != NULL) { g_cancellable_cancel (self->request->cancellable); }
    label_text (self, self->status, _("Preview timed out."), generation);
    if (current (self, generation)) { set_loading (self, FALSE); }
    return G_SOURCE_REMOVE;
}

/* Runs off the GTK thread. No pathname is read until the opened descriptor
 * is regular and matches the metadata identity; O_NONBLOCK also makes a FIFO
 * replacement safe. Canceled workers retain their slot until actually done. */
static void
read_text (GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
    if (g_task_return_error_if_cancelled (task)) { return; }
    PreviewRequest *request = data;
    g_autofree char *path = g_file_get_path (request->file);
    int fd = g_open (path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, 0);
    struct stat info;
    if (fd < 0 || fstat (fd, &info) != 0 || !S_ISREG (info.st_mode) ||
        !g_file_info_has_attribute (request->info, G_FILE_ATTRIBUTE_UNIX_DEVICE) ||
        !g_file_info_has_attribute (request->info, G_FILE_ATTRIBUTE_UNIX_INODE) ||
        (guint64) info.st_dev != g_file_info_get_attribute_uint32 (request->info, G_FILE_ATTRIBUTE_UNIX_DEVICE) ||
        (guint64) info.st_ino != g_file_info_get_attribute_uint64 (request->info, G_FILE_ATTRIBUTE_UNIX_INODE))
    {
        if (fd >= 0) { close (fd); }
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "File changed or cannot be opened safely");
        return;
    }
    g_autoptr (GByteArray) bytes = g_byte_array_new ();
    while (bytes->len < TEXT_INPUT_LIMIT && !g_cancellable_is_cancelled (cancellable))
    {
        guint8 buffer[4096];
        ssize_t count = read (fd, buffer, MIN (sizeof buffer, TEXT_INPUT_LIMIT - bytes->len));
        if (count > 0) { g_byte_array_append (bytes, buffer, count); }
        else if (count == 0) { break; }
        else if (errno != EINTR)
        {
            close (fd);
            g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "File could not be read");
            return;
        }
    }
    close (fd);
    g_task_return_pointer (task, g_byte_array_free_to_bytes (g_steal_pointer (&bytes)), (GDestroyNotify) g_bytes_unref);
}

static void
text_ready (GObject *source, GAsyncResult *result, gpointer data)
{
    PreviewRequest *request = data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GBytes) bytes = g_task_propagate_pointer (G_TASK (result), &error);
    g_autoptr (NautilusMillerPreview) self = g_weak_ref_get (&request->owner);
    if (self != NULL && current (self, request->generation) && !self->timed_out && work_allowed (self))
    {
        if (bytes == NULL)
        {
            if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            {
                label_text (self, self->status, _("File changed or could not be read."), request->generation);
            }
        }
        else
        {
            gsize length;
            const char *text = g_bytes_get_data (bytes, &length), *invalid;
            gboolean valid = g_utf8_validate (text, length, &invalid);
            if (!valid && length == TEXT_INPUT_LIMIT && length - (invalid - text) <= 3 &&
                (guchar) *invalid >= 0xc2 &&
                g_utf8_get_char_validated (invalid, length - (invalid - text)) == (gunichar) -2)
            {
                length = invalid - text;
                valid = TRUE;
            }
            for (gsize i = 0; valid && i < length; i++)
            {
                guchar c = text[i];
                if (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f') { valid = FALSE; }
            }
            if (!valid)
            {
                label_text (self, self->status, _("Text preview is unavailable for binary or invalid UTF-8 data."), request->generation);
            }
            else
            {
                gsize display = MIN (length, TEXT_DISPLAY_LIMIT);
                while (display > 0 && display < length && ((guchar) text[display] & 0xc0) == 0x80) { display--; }
                g_autoptr (GtkTextBuffer) buffer = g_object_ref (gtk_text_view_get_buffer (self->text_view));
                gtk_text_buffer_set_text (buffer, text != NULL ? text : "", display);
                label_text (self, self->status, length > display ? _("Truncated text preview") : _("Text preview"), request->generation);
            }
        }
    }
    request_finished (request);
}

static void
metadata_ready (GObject *source, GAsyncResult *result, gpointer data)
{
    PreviewRequest *request = data;
    g_autoptr (GError) error = NULL;
    request->info = g_file_query_info_finish (G_FILE (source), result, &error);
    g_autoptr (NautilusMillerPreview) self = g_weak_ref_get (&request->owner);
    if (self == NULL || !current (self, request->generation) || self->timed_out || !work_allowed (self))
    {
        request_finished (request);
        return;
    }
    guint64 generation = request->generation;
    if (request->info == NULL)
    {
        label_text (self, self->status, _("File information is unavailable."), generation);
        request_finished (request);
        return;
    }
    GFileType type = g_file_info_has_attribute (request->info, G_FILE_ATTRIBUTE_STANDARD_TYPE) ?
        g_file_info_get_file_type (request->info) : G_FILE_TYPE_UNKNOWN;
    gboolean link = type == G_FILE_TYPE_SYMBOLIC_LINK ||
        (g_file_info_has_attribute (request->info, G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK) &&
         g_file_info_get_is_symlink (request->info));
    const char *display_name = g_file_info_has_attribute (request->info, G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME) ?
        g_file_info_get_display_name (request->info) : NULL;
    g_autofree char *name = display_name != NULL ? g_utf8_make_valid (display_name, -1) :
        g_strdup (gtk_label_get_text (self->title));
    g_autofree char *description = link ? g_strdup (_("Symbolic link")) :
        type == G_FILE_TYPE_DIRECTORY ? g_strdup (_("Folder")) :
        type == G_FILE_TYPE_SPECIAL ? g_strdup (_("Special file")) :
        type != G_FILE_TYPE_REGULAR ? g_strdup (_("File type unavailable")) : g_content_type_get_description (request->mime);
    g_autofree char *size = type == G_FILE_TYPE_REGULAR && !link ?
        (g_file_info_has_attribute (request->info, G_FILE_ATTRIBUTE_STANDARD_SIZE) ?
         g_format_size (g_file_info_get_size (request->info)) : g_strdup (_("Size unavailable"))) :
        g_strdup (_("Size not calculated"));
    if (!label_text (self, self->title, name, generation) || !label_text (self, self->kind, description, generation) ||
        !label_text (self, self->size, size, generation)) { request_finished (request); return; }
    if (g_file_info_has_attribute (request->info, G_FILE_ATTRIBUTE_TIME_MODIFIED))
    {
        g_autoptr (GDateTime) time = g_date_time_new_from_unix_local (
            g_file_info_get_attribute_uint64 (request->info, G_FILE_ATTRIBUTE_TIME_MODIFIED));
        g_autofree char *formatted = time != NULL ? g_date_time_format (time, "%x %X") : NULL;
        g_autofree char *modified = formatted != NULL ? g_strdup_printf (_("Modified: %s"), formatted) : g_strdup ("");
        if (!label_text (self, self->modified, modified, generation)) { request_finished (request); return; }
    }
    gtk_image_set_from_icon_name (self->icon, type == G_FILE_TYPE_DIRECTORY ? "folder-symbolic" : "text-x-generic-symbolic");
    if (!current (self, generation)) { request_finished (request); return; }
    if (type != G_FILE_TYPE_REGULAR || link)
    {
        label_text (self, self->status, _("Metadata preview"), generation);
    }
    else if (!g_file_is_native (request->file))
    {
        label_text (self, self->status, _("Remote content preview is disabled."), generation);
    }
    else if (g_content_type_is_a (request->mime, "text/plain") || g_str_has_prefix (request->mime, "text/"))
    {
        if (label_text (self, self->status, _("Loading text preview…"), generation) &&
            !self->timed_out && !g_cancellable_is_cancelled (request->cancellable))
        {
            g_autoptr (GTask) task = g_task_new (NULL, request->cancellable, text_ready, request);
            g_task_set_task_data (task, request, NULL);
            /* Default return-on-cancel is FALSE: only its actual completion
             * releases the in-flight slot, even after the UI times out. */
            g_task_run_in_thread (task, read_text);
            return;
        }
    }
    else { label_text (self, self->status, _("Preview is unavailable for this file type."), generation); }
    request_finished (request);
}

static void
start_request (NautilusMillerPreview *self)
{
    if (self->request != NULL || !self->queued || !work_allowed (self) || self->files == NULL) { return; }
    PreviewRequest *request = g_new0 (PreviewRequest, 1);
    g_weak_ref_init (&request->owner, self);
    request->generation = self->generation;
    request->file = nautilus_file_get_location (self->files->data);
    const char *mime = nautilus_file_get_mime_type (self->files->data);
    request->mime = mime != NULL && nautilus_file_check_if_ready (self->files->data, NAUTILUS_FILE_ATTRIBUTE_INFO) ?
        g_strdup (mime) : g_content_type_guess (
        nautilus_file_get_display_name (self->files->data), NULL, 0, NULL);
    request->cancellable = g_cancellable_new ();
    self->request = request;
    g_file_query_info_async (request->file, INFO_ATTRIBUTES, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                             G_PRIORITY_DEFAULT, request->cancellable, metadata_ready, request);
}

static gboolean
aggregate_selection (gpointer data)
{
    g_autoptr (NautilusMillerPreview) self = g_object_ref (data);
    guint64 generation = self->generation;
    gint64 end = g_get_monotonic_time () + 4000;
    for (guint n = 0; self->aggregate_next != NULL && n < 128 && g_get_monotonic_time () < end; n++)
    {
        NautilusFile *file = self->aggregate_next->data;
        /* This legacy predicate is TRUE when the cached size is unknown. */
        if (nautilus_file_get_file_type (file) == G_FILE_TYPE_REGULAR &&
            !nautilus_file_is_symbolic_link (file) && !nautilus_file_can_get_size (file))
        {
            guint64 size = nautilus_file_get_size (file);
            if (G_MAXUINT64 - self->known_size >= size) { self->known_size += size; }
            else { self->partial = TRUE; }
        }
        else { self->partial = TRUE; }
        self->aggregate_next = self->aggregate_next->next;
    }
    if (self->aggregate_next != NULL) { return G_SOURCE_CONTINUE; }
    self->aggregate_idle = 0;
    g_autofree char *size = g_format_size (self->known_size);
    g_autofree char *summary = self->partial ? g_strdup_printf (_("Known file size: %s (partial)"), size) :
        g_strdup_printf (_("Total file size: %s"), size);
    label_text (self, self->size, summary, generation);
    if (current (self, generation)) { set_loading (self, FALSE); }
    return G_SOURCE_REMOVE;
}

static void
refresh (NautilusMillerPreview *self)
{
    if (self->disposed) { return; }
    g_autoptr (NautilusMillerPreview) hold = g_object_ref (self);
    guint64 generation = ++self->generation;
    g_clear_handle_id (&self->deadline, g_source_remove);
    g_clear_handle_id (&self->aggregate_idle, g_source_remove);
    if (self->request != NULL) { g_cancellable_cancel (self->request->cancellable); }
    self->queued = self->timed_out = FALSE;
    /* A selection may change from an old buffer's changed handler. Replace
     * that buffer instead of recursively mutating its live text iterators. */
    g_autoptr (GtkTextBuffer) empty = gtk_text_buffer_new (NULL);
    gtk_text_view_set_buffer (self->text_view, empty);
    if (!current (self, generation)) { return; }
    if (!label_text (self, self->kind, "", generation) || !label_text (self, self->size, "", generation) ||
        !label_text (self, self->modified, "", generation)) { return; }
    guint count = g_list_length (self->files);
    g_autofree char *title = count == 1 ? g_utf8_make_valid (nautilus_file_get_display_name (self->files->data), -1) :
        count == 0 ? g_strdup (_("Preview")) : g_strdup_printf (ngettext ("%u item", "%u items", count), count);
    if (!label_text (self, self->title, title, generation)) { return; }
    if (count == 0 || !work_allowed (self))
    {
        label_text (self, self->status, count == 0 ? _("Select a file to preview") : _("Preview paused"), generation);
        if (current (self, generation)) { set_loading (self, FALSE); }
        return;
    }
    if (count > 1)
    {
        if (!label_text (self, self->status, _("Selection metadata"), generation)) { return; }
        self->aggregate_next = self->files;
        self->known_size = 0;
        self->partial = FALSE;
        self->aggregate_idle = g_idle_add (aggregate_selection, self);
    }
    else
    {
        if (!label_text (self, self->status, _("Loading information…"), generation)) { return; }
        self->queued = TRUE;
        self->deadline = g_timeout_add_seconds (5, request_timed_out, self);
        start_request (self);
    }
    if (current (self, generation)) { set_loading (self, TRUE); }
}

void
nautilus_miller_preview_set_selection (NautilusMillerPreview *self, NautilusFileList *files)
{
    g_autoptr (NautilusMillerPreview) hold = g_object_ref (self);
    NautilusFileList *copy = nautilus_file_list_copy (files);
    g_autolist (NautilusFile) previous = self->files;
    self->files = copy;
    refresh (self);
}

void
nautilus_miller_preview_set_active (NautilusMillerPreview *self, gboolean active)
{
    if (self->active != active) { self->active = active; refresh (self); }
}

static void
preview_map (GtkWidget *widget)
{
    GTK_WIDGET_CLASS (nautilus_miller_preview_parent_class)->map (widget);
    refresh (NAUTILUS_MILLER_PREVIEW (widget));
}

static void
preview_unmap (GtkWidget *widget)
{
    GTK_WIDGET_CLASS (nautilus_miller_preview_parent_class)->unmap (widget);
    refresh (NAUTILUS_MILLER_PREVIEW (widget));
}

static void
preview_dispose (GObject *object)
{
    NautilusMillerPreview *self = NAUTILUS_MILLER_PREVIEW (object);
    self->disposed = TRUE;
    self->generation++;
    self->queued = FALSE;
    g_clear_handle_id (&self->deadline, g_source_remove);
    g_clear_handle_id (&self->aggregate_idle, g_source_remove);
    if (self->request != NULL) { g_cancellable_cancel (self->request->cancellable); self->request = NULL; }
    g_clear_pointer (&self->files, nautilus_file_list_free);
    G_OBJECT_CLASS (nautilus_miller_preview_parent_class)->dispose (object);
}

static void
preview_get_property (GObject *object, guint id, GValue *value, GParamSpec *pspec)
{
    if (id == 1) { g_value_set_boolean (value, NAUTILUS_MILLER_PREVIEW (object)->loading); }
    else { G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec); }
}

static void
nautilus_miller_preview_class_init (NautilusMillerPreviewClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = preview_dispose;
    G_OBJECT_CLASS (klass)->get_property = preview_get_property;
    GTK_WIDGET_CLASS (klass)->map = preview_map;
    GTK_WIDGET_CLASS (klass)->unmap = preview_unmap;
    loading_property = g_param_spec_boolean ("loading", NULL, NULL, FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (G_OBJECT_CLASS (klass), 1, loading_property);
}

static GtkLabel *
append_label (GtkBox *box, const char *text)
{
    GtkWidget *label = gtk_label_new (text);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_label_set_wrap (GTK_LABEL (label), TRUE);
    gtk_box_append (box, label);
    return GTK_LABEL (label);
}

static void
nautilus_miller_preview_init (NautilusMillerPreview *self)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top (box, 12); gtk_widget_set_margin_bottom (box, 12);
    gtk_widget_set_margin_start (box, 12); gtk_widget_set_margin_end (box, 12);
    self->title = append_label (GTK_BOX (box), _("Preview"));
    gtk_widget_add_css_class (GTK_WIDGET (self->title), "title-3");
    self->icon = GTK_IMAGE (gtk_image_new_from_icon_name ("text-x-generic-symbolic"));
    gtk_image_set_pixel_size (self->icon, 64);
    gtk_box_append (GTK_BOX (box), GTK_WIDGET (self->icon));
    self->kind = append_label (GTK_BOX (box), "");
    self->size = append_label (GTK_BOX (box), "");
    self->modified = append_label (GTK_BOX (box), "");
    self->status = append_label (GTK_BOX (box), _("Select a file to preview"));
    gtk_widget_add_css_class (GTK_WIDGET (self->status), "dim-label");
    GtkWidget *view = gtk_text_view_new ();
    self->text_view = GTK_TEXT_VIEW (view);
    gtk_text_view_set_editable (GTK_TEXT_VIEW (view), FALSE);
    gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view), FALSE);
    gtk_text_view_set_monospace (GTK_TEXT_VIEW (view), TRUE);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (view), GTK_WRAP_WORD_CHAR);
    gtk_accessible_update_property (GTK_ACCESSIBLE (view), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Text preview"), -1);
    GtkWidget *scroller = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroller), view);
    gtk_widget_set_vexpand (scroller, TRUE);
    gtk_box_append (GTK_BOX (box), scroller);
    adw_bin_set_child (ADW_BIN (self), box);
}

GtkWidget *
nautilus_miller_preview_new (void)
{
    return g_object_new (NAUTILUS_TYPE_MILLER_PREVIEW, NULL);
}
