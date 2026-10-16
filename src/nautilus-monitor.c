/*
 *  nautilus-monitor.c: file and directory change monitoring for nautilus
 *
 *  Copyright (C) 2000, 2001 Eazel, Inc.
 *  Copyright (C) 2016 Red Hat
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *  Authors: Seth Nickell <seth@eazel.com>
 *           Darin Adler <darin@bentspoon.com>
 *           Alex Graveley <alex@ximian.com>
 *           Carlos Soriano <csoriano@gnome.org>
 */

#include <config.h>
#include "nautilus-monitor.h"
#include "nautilus-file-changes-queue.h"
#include "nautilus-file-utilities.h"
#include "nautilus-directory-private.h"

#include <gio/gio.h>

struct NautilusMonitor
{
    gatomicrefcount references;
    GFileMonitor *monitor;
    GVolumeMonitor *volume_monitor;
    GFile *location;
    GCancellable *cancellable;
    gboolean cancelled;
};

static gboolean call_consume_changes_idle_id = 0;

static gboolean
call_consume_changes_idle_cb (gpointer not_used)
{
    nautilus_file_changes_consume_changes ();
    call_consume_changes_idle_id = 0;
    return FALSE;
}

static void
schedule_call_consume_changes (void)
{
    if (call_consume_changes_idle_id == 0)
    {
        call_consume_changes_idle_id =
            g_idle_add (call_consume_changes_idle_cb, NULL);
    }
}

static void
mount_removed (GVolumeMonitor *volume_monitor,
               GMount         *mount,
               gpointer        user_data)
{
    NautilusMonitor *monitor = user_data;
    GFile *mount_location;

    mount_location = g_mount_get_root (mount);
    if (g_file_equal (monitor->location, mount_location) ||
        g_file_has_prefix (monitor->location, mount_location))
    {
        nautilus_file_changes_queue_file_unmounted (monitor->location);
        schedule_call_consume_changes ();
    }

    g_object_unref (mount_location);
}

static void
dir_changed (GFileMonitor      *monitor,
             GFile             *child,
             GFile             *other_file,
             GFileMonitorEvent  event_type,
             gpointer           user_data)
{
    switch (event_type)
    {
        default:
        case G_FILE_MONITOR_EVENT_CHANGED:
        {
            /* ignore */
        }
        break;

        case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
        case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
        {
            nautilus_file_changes_queue_file_changed (child);
        }
        break;

        case G_FILE_MONITOR_EVENT_UNMOUNTED:
        {
            nautilus_file_changes_queue_file_unmounted (child);
        }
        break;

        case G_FILE_MONITOR_EVENT_DELETED:
        {
            nautilus_file_changes_queue_file_removed (child);
        }
        break;

        case G_FILE_MONITOR_EVENT_CREATED:
        {
            nautilus_file_changes_queue_file_added (child);
        }
        break;

        case G_FILE_MONITOR_EVENT_MOVED_IN:
        {
            if (other_file != NULL)
            {
                nautilus_file_changes_queue_file_moved (other_file, child);
            }
            else
            {
                nautilus_file_changes_queue_file_added (child);
            }
        }
        break;

        case G_FILE_MONITOR_EVENT_MOVED_OUT:
        {
            if (other_file != NULL)
            {
                nautilus_file_changes_queue_file_moved (child, other_file);
            }
            else
            {
                nautilus_file_changes_queue_file_removed (child);
            }
        }
        break;

        case G_FILE_MONITOR_EVENT_RENAMED:
        {
            nautilus_file_changes_queue_file_moved (child, other_file);
        }
        break;
    }

    schedule_call_consume_changes ();
}

static void
monitor_unref (gpointer data)
{
    NautilusMonitor *monitor = data;
    if (g_atomic_ref_count_dec (&monitor->references))
    {
        g_clear_object (&monitor->location);
        g_clear_object (&monitor->cancellable);
        g_slice_free (NautilusMonitor, monitor);
    }
}

static void
cancel_file_monitor (gpointer data)
{
    GFileMonitor *monitor = data;
    g_file_monitor_cancel (monitor);
    g_object_unref (monitor);
}

static void
create_remote_monitor (GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
    NautilusMonitor *monitor = data;
    g_autoptr (GError) error = NULL;
    GFileMonitor *result = g_file_monitor_directory (monitor->location,
        G_FILE_MONITOR_WATCH_MOUNTS | G_FILE_MONITOR_WATCH_MOVES, cancellable, &error);

    if (result != NULL)
    {
        g_task_return_pointer (task, result, cancel_file_monitor);
    }
    else if (error != NULL) { g_task_return_error (task, g_steal_pointer (&error)); }
    else { g_task_return_pointer (task, NULL, NULL); }
}

static void
remote_monitor_ready (GObject *source, GAsyncResult *result, gpointer data)
{
    NautilusMonitor *monitor = data;
    GFileMonitor *file_monitor = g_task_propagate_pointer (G_TASK (result), NULL);
    if (monitor->cancelled)
    {
        if (file_monitor != NULL) { cancel_file_monitor (file_monitor); }
        return;
    }
    monitor->monitor = file_monitor;
    if (file_monitor != NULL)
    {
        g_signal_connect (file_monitor, "changed", G_CALLBACK (dir_changed), monitor);
        /* Enumeration can finish before remote watch setup. Reconcile once
         * after connecting, so changes during that gap are not lost. */
        g_autoptr (NautilusDirectory) directory = nautilus_directory_get_existing (monitor->location);
        if (directory != NULL) { nautilus_directory_force_reload (directory); }
    }
}

NautilusMonitor *
nautilus_monitor_directory (GFile *location)
{
    NautilusMonitor *ret = g_slice_new0 (NautilusMonitor);
    g_atomic_ref_count_init (&ret->references);
    ret->location = g_object_ref (location);

    /* Currently, some GVfs backends which support monitoring never emit
     * G_FILE_MONITOR_EVENT_UNMOUNTED, nor _DELETED events when the location
     * is unmounted. Use GVolumeMonitor in addition to GFileMonitor.
     */
    if (!g_file_is_native (location))
    {
        ret->volume_monitor = g_volume_monitor_get ();
        ret->cancellable = g_cancellable_new ();
        g_autoptr (GTask) task = g_task_new (NULL, ret->cancellable, remote_monitor_ready, ret);
        g_atomic_ref_count_inc (&ret->references);
        g_task_set_task_data (task, ret, monitor_unref);
        /* Workers have no thread-default context: monitor events are queued
         * on the application's global main context, as with native watches. */
        g_task_run_in_thread (task, create_remote_monitor);
    }
    else
    {
        ret->monitor = g_file_monitor_directory (location,
            G_FILE_MONITOR_WATCH_MOUNTS | G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
    }

    if (ret->monitor != NULL)
    {
        g_signal_connect (ret->monitor, "changed",
                          G_CALLBACK (dir_changed), ret);
    }

    if (ret->volume_monitor != NULL)
    {
        g_signal_connect (ret->volume_monitor, "mount-removed",
                          G_CALLBACK (mount_removed), ret);
    }

    /* We return a monitor even on failure, so we can avoid later trying again */
    return ret;
}

void
nautilus_monitor_cancel (NautilusMonitor *monitor)
{
    monitor->cancelled = TRUE;
    if (monitor->cancellable != NULL) { g_cancellable_cancel (monitor->cancellable); }
    if (monitor->monitor != NULL)
    {
        g_signal_handlers_disconnect_by_func (monitor->monitor, dir_changed, monitor);
        g_clear_pointer (&monitor->monitor, cancel_file_monitor);
    }

    if (monitor->volume_monitor != NULL)
    {
        g_signal_handlers_disconnect_by_func (monitor->volume_monitor, mount_removed, monitor);
        g_clear_object (&monitor->volume_monitor);
    }

    monitor_unref (monitor);
}
