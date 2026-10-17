/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <adwaita.h>
#include "miller-test-input.h"
#include "nautilus-files-view.h"
#include "nautilus-window-slot.h"
#include "nautilus-progress-info-manager.h"

#define MILLER_TEST_OPERATION_PAYLOAD "Native clipboard payload: 雪\n"

typedef struct
{
    GtkWidget *window;
    NautilusWindowSlot *slot;
    GFile *root, *source_dir, *source_file, *parent, *child, *sibling;
    NautilusProgressInfoManager *manager;
    GPtrArray *jobs;
    GSubprocess *receiver;
    GDataInputStream *receiver_output;
    guint view_id;
    guint paste_keys, paste_actions;
} OperationFixture;

/* Borrowed verified input; the test main owns it through fixture teardown. */
void operations_set_input (MillerTestInput *input);
void operations_pointer_to (OperationFixture *f, GtkWidget *widget, double widget_x, double widget_y);

void
operations_pump (guint milliseconds);
void
operations_owned (OperationFixture *f, GFile *file);
void
operations_assert_bytes (OperationFixture *f, GFile *file, const char *expected);
void
operations_settle (OperationFixture *f);
NautilusFilesView *
operations_view_for_directory (OperationFixture *f, GFile *directory);
void
operations_select (OperationFixture *f, NautilusFilesView *view, GFile *location);
void
operations_wait_active (OperationFixture *f);
void
operations_assert_focus (OperationFixture *f, NautilusFilesView *view);
void
operations_chord (guint key, gboolean shift);
void
operations_key (guint key);
void
operations_type (const char *text);
GtkWidget *
operations_find_type (GtkWidget *widget, GType type);
GtkWidget *
operations_find_label (GtkWidget *widget, const char *text);
void
operations_click (OperationFixture *f, GtkWidget *widget, guint button);
void
operations_setup (OperationFixture *f, gconstpointer view_id);
void
operations_teardown (OperationFixture *f, gconstpointer unused);
void
operations_wait_file (OperationFixture *f, NautilusFilesView *view, GFile *destination);
void
operations_wait_copy (OperationFixture *f, NautilusFilesView *view, GFile *destination);
NautilusFilesView *
operations_open_parent (OperationFixture *f);
void
operations_assert_sentinels (OperationFixture *f);
void
operations_child_marker (GDataInputStream *stream, const char *expected);
void
operations_child_exited (GObject *source, GAsyncResult *result, gpointer data);
void
operations_assert_private_home (void);
void
operations_stop_receiver (OperationFixture *f);
