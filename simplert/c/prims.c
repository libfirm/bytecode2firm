#include "types.h"

extern vtable_t _ZTVN4java4lang5ClassE;

java_lang_Class _Jv_intClass;
java_lang_Class _Jv_shortClass;
java_lang_Class _Jv_longClass;
java_lang_Class _Jv_floatClass;
java_lang_Class _Jv_doubleClass;

/* TODO: correct type */
int _Jv_soleCompiledEngine;

static void init_rtti(java_lang_Class *rtti, const char *name)
{
	rtti->base.vptr = &_ZTVN4java4lang5ClassE;
	(void)name; // TODO
}

void init_prim_rtti(void)
{
	init_rtti(&_Jv_intClass,    "int");
	init_rtti(&_Jv_shortClass,  "short");
	init_rtti(&_Jv_longClass,   "long");
	init_rtti(&_Jv_floatClass,  "float");
	init_rtti(&_Jv_doubleClass, "double");
}
