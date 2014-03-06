#include "types.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

extern extended_vtable_t _ZTVN4java4lang5ClassE;

java_lang_Class _Jv_intClass;
java_lang_Class _Jv_booleanClass;
java_lang_Class _Jv_byteClass;
java_lang_Class _Jv_charClass;
java_lang_Class _Jv_shortClass;
java_lang_Class _Jv_longClass;
java_lang_Class _Jv_floatClass;
java_lang_Class _Jv_doubleClass;
java_lang_Class objarray_class;

/* TODO: correct type */
int _Jv_soleCompiledEngine;

unsigned calc_string_hash(const char *chars, size_t len)
{
	unsigned hash = 0;
	for (size_t i = 0; i < len; ++i) {
		unsigned char c = chars[i];
		// FIXME: fails for non-ascii codepoints
		assert(c <= 127);
		hash = (hash * 31) + c;
	}
	return hash;
}

utf8_const *utf8_const_from_c_chars(const char *str)
{
	size_t len = strlen(str);
	utf8_const *cnst = calloc(1, sizeof(utf8_const) + len);
	assert((size_t)(uint16_t)len == len);
	cnst->len = len;
	memcpy(cnst->data, str, len);
	cnst->hash = calc_string_hash(cnst->data, len);
	return cnst;
}

static void init_rtti(java_lang_Class *rtti, size_t size, char sig,
                      const char *name)
{
	assert((size_t)(int)size == size);

	memset(rtti, 0, sizeof(*rtti));
	rtti->base.vptr     = &_ZTVN4java4lang5ClassE.vtable;
	rtti->name          = utf8_const_from_c_chars(name);
	rtti->vtable        = (vtable_t*)-1;
	rtti->size_in_bytes = (int)size;
	rtti->state         = JV_STATE_DONE;
	rtti->method_count  = (char)sig;
}

void init_prim_rtti(void)
{
	init_rtti(&_Jv_intClass,     sizeof(jint),     'I', "int");
	init_rtti(&_Jv_booleanClass, sizeof(jboolean), 'Z', "boolean");
	init_rtti(&_Jv_byteClass,    sizeof(jbyte),    'B', "byte");
	init_rtti(&_Jv_charClass,    sizeof(jchar),    'C', "char");
	init_rtti(&_Jv_shortClass,   sizeof(jshort),   'S', "short");
	init_rtti(&_Jv_longClass,    sizeof(jlong),    'J', "long");
	init_rtti(&_Jv_floatClass,   sizeof(jfloat),   'F', "float");
	init_rtti(&_Jv_doubleClass,  sizeof(jdouble),  'D', "double");
}
