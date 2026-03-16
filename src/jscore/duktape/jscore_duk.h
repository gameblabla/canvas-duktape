#pragma once
#include "common/types.h"

/* Populate *iface with Duktape JS core function pointers. */
void jscore_duk_init_iface(JSCoreInterface* iface);
