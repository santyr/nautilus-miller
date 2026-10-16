/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <gio/gio.h>

typedef struct _MillerTestInput MillerTestInput;
/* Metadata validation alone does not authorize delivery: new() also verifies
 * the actual private bus owner/process and retains a connection to that owner. */
gboolean miller_test_input_validate (const char *marker, const char *runtime,
                                     const char *display, const char *address,
                                     guint *pid, GError **error);
MillerTestInput *miller_test_input_new (GError **error);
void miller_test_input_free (MillerTestInput *input);
gboolean miller_test_input_key (MillerTestInput *input, guint keysym, gboolean pressed, GError **error);
gboolean miller_test_input_keycode (MillerTestInput *input, guint keycode, gboolean pressed, GError **error);
gboolean miller_test_input_button (MillerTestInput *input, guint button, gboolean pressed, GError **error);
gboolean miller_test_input_motion (MillerTestInput *input, double x, double y, GError **error);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MillerTestInput, miller_test_input_free)
