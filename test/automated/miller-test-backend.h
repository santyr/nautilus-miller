/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <gio/gio.h>

/* Test-only controls for the guarded with-delayed-backend.py fixture. */
GKeyFile *miller_test_backend_metadata (void);
void miller_test_backend_control (const char *request);
