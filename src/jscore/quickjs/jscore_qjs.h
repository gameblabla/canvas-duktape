#pragma once
#include "common/types.h"

/* Populate *iface with QuickJS JS core function pointers. */
void jscore_qjs_init_iface(JSCoreInterface* iface);
