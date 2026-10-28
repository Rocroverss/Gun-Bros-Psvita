/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file patch.c
 * @brief Composition unit for the categorized Gun Bros compatibility patches.
 */

#include <kubridge.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <so_util/so_util.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <sys/time.h>

#include "audio.h"
#include "utils/data_check.h"
#include "utils/perf_trace.h"
#include "utils/classic_dialog.h"

extern so_module so_mod;

/* shared.inc.c uses the bounded network logger before network.inc.c defines it. */
static void patch_log_once(const char *label, const char *result);
static uintptr_t hook_symbol_store_arm_trampoline_checked(
    const char *label, const char *symbol, uintptr_t replacement,
    so_hook *out, uint32_t expected_first, uint32_t expected_second);

#include "patch/shared.inc.c"
#include "patch/audio.inc.c"
#include "patch/objects.inc.c"
#include "patch/weapon_mastery.inc.c"
#include "patch/shops.inc.c"
#include "patch/rendering.inc.c"
#include "patch/menus.inc.c"
#include "patch/planet_notifications.inc.c"
#include "patch/classic.inc.c"
#include "patch/graphics.inc.c"
#include "patch/hook_helpers.inc.c"
#include "patch/network.inc.c"
#include "patch/bootstrap_guards.inc.c"
#include "patch/gameplay.inc.c"
#include "patch/install.inc.c"
