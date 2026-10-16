/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <gio/gio.h>

#define MILLER_DEFAULT_WIDTH_KEY "miller-default-column-width"
#define MILLER_WIDTHS_KEY "miller-column-widths"
#define MILLER_MIN_WIDTH 180
#define MILLER_MAX_WIDTH 1200

int nautilus_miller_width_get (GSettings *settings, GFile *location);
void nautilus_miller_width_set (GSettings *settings, GFile *location, int width);
void nautilus_miller_width_reset (GSettings *settings, GFile *location);
