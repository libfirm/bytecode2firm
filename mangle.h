#ifndef OO_MANGLE_H
#define OO_MANGLE_H

#include <stdbool.h>
#include <libfirm/firm.h>
#include "adt/obst.h"

/**
 * Mangle function type into function names to make polymorphic (overloaded)
 * functions unique
 *
 * The compression table contains prefixes of currently mangled name.
 * Pointers to and arrays of a specific type, and the JArray keyword, cause an additional entry.
 * Example: mangling
 * JArray<java::lang::Object*>* java::lang::ClassLoader::putDeclaredAnnotations(java::lang::Class*, int, int, int, JArray<java::lang::Object*>*)
 * S_  = java
 * S0_ = java/lang
 * S1_ = java/lang/ClassLoader
 * S2_ = JArray
 * S3_ = java/lang/Object
 * S4_ = *java/lang/Object
 * S5_ = <*java/lang/Object>
 * S6_ = *<*java/lang/Object>
 * => _ZN4java4lang11ClassLoader22putDeclaredAnnotationsEJP6JArrayIPNS0_6ObjectEEPNS0_5ClassEiiiS6_
 */

ident *mangle_member_name(const char *defining_class, const char *member_name, const char *member_signature);
ident *mangle_vtable_name(const char *classname);
ident *mangle_rtti_name(const char *classname);

void mangle_init(void);
void mangle_deinit(void);

#endif
