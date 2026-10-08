#pragma once
#include <IBinTools.h>
#include "bttest.h"

// Supplied by the host (extension or standalone runner)
extern SourceMod::IBinTools *g_pBinTools;
void bt_print(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Runs every direct case; returns the number of failures
int BtRunDirect(bool verbose, int *passed);

// Fake object + vtable, also used by the plugin natives
extern VObj g_obj;
extern void *g_vtable[64];
