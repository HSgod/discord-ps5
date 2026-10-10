/*
 * Accord - process-lifetime heap: the counters it keeps.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Fills in the arena's live and peak bytes, outstanding blocks and the number
 * of allocations it refused. Any pointer may be NULL. Safe to call before the
 * arena exists: everything reads zero. */
void accord_heap_stats(size_t *live_bytes, size_t *peak_bytes, size_t *blocks, size_t *failures);

#ifdef __cplusplus
}
#endif
