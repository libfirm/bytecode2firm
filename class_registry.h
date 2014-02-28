#ifndef CLASS_REGISTRY_H
#define CLASS_REGISTRY_H

#include <libfirm/firm.h>
#include "class_file.h"
#include "types.h"

void class_registry_init(void);
ir_type *class_registry_get(const char *classname);
void class_registry_set(const char *classname, ir_type *type);

#endif
