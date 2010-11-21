#ifndef MANGLE_H
#define MANGLE_H

#include <libfirm/firm.h>

/**
 * Mangle function type into function names to make polymorphic (overloaded)
 * functions unique
 */
ident *mangle_entity_name(ir_entity *entity);

ident *mangle_vtable_name(ir_type *clazz);

void mangle_init(void);
void mangle_set_primitive_type_name(ir_type *type, const char *name);
void mangle_add_name_substitution(const char *name, const char *mangled);
void mangle_deinit(void);

#endif
