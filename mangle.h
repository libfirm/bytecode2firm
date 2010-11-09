#ifndef MANGLE_H
#define MANGLE_H

#include <libfirm/firm.h>

/**
 * Mangle function type into function names to make polymorphic (overloaded)
 * functions unique
 */
ident *mangle_entity_name(ir_entity *entity);

ident *mangle_vtable_name(ir_type *clazz);

void init_mangle(void);
void deinit_mangle(void);

#endif
