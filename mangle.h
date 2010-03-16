#ifndef MANGLE_H
#define MANGLE_H

#include <libfirm/firm.h>

/**
 * Mangle function type into function names to make polymorphic (overloaded)
 * functions unique
 */
ident *mangle_entity_name(ir_type *owner, ir_type *type, ident *id);

/**
 * special name mangler for "native" functions to make them more similar
 * to JNI names
 */
ident *mangle_native_func(ir_type *owner, ir_type *type, ident *id);

void init_mangle(void);
void deinit_mangle(void);

#endif
