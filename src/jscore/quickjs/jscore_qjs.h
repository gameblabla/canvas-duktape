#pragma once
#include "common/types.h"

/* Populate *iface with QuickJS JS core function pointers. */
void jscore_qjs_init_iface(JSCoreInterface* iface);

/* Set the base directory for resolving relative paths in XMLHttpRequest */
void jscore_qjs_set_base_dir(const char *dir);
