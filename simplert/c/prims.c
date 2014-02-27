#include "types.h"

#include <assert.h>
#include <stddef.h>

extern vtable_t _ZTVN4java4lang5ClassE;

java_lang_Class _Jv_intClass;
java_lang_Class _Jv_shortClass;
java_lang_Class _Jv_longClass;
java_lang_Class _Jv_floatClass;
java_lang_Class _Jv_doubleClass;

/* TODO: correct type */
int _Jv_soleCompiledEngine;

static void init_rtti(java_lang_Class *rtti, size_t size, const char *name)
{
	rtti->base.vptr = &_ZTVN4java4lang5ClassE;
	(void)name; // TODO
	rtti->vtable        = (vtable_t*)-1;
	assert((size_t)(int)size == size);
	rtti->size_in_bytes = (int)size;
}

void init_prim_rtti(void)
{
	init_rtti(&_Jv_intClass,    sizeof(jint),    "int");
	init_rtti(&_Jv_shortClass,  sizeof(jshort),  "short");
	init_rtti(&_Jv_longClass,   sizeof(jlong),   "long");
	init_rtti(&_Jv_floatClass,  sizeof(jfloat),  "float");
	init_rtti(&_Jv_doubleClass, sizeof(jdouble), "double");
}
