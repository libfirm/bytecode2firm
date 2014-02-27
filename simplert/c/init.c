#include "types.h"

#include <stdio.h>
#include <stdbool.h>

enum {
	JV_STATE_NOTHING = 0,
	JV_STATE_DONE    = 14
};

static const utf8_const clinit_name = { 0x0ea9, 8, "<clinit>" };
static const utf8_const void_sig    = { 0x9b75, 3, "()V"      };

static bool utf8_consts_equal(const utf8_const *c1, const utf8_const *c2)
{
	if (c1->hash != c2->hash)
		return false;
	uint16_t len = c1->len;
	if (len != c2->len)
		return false;
	const char *d1 = c1->data;
	const char *d2 = c2->data;
	for (uint16_t i = 0; i < len; ++i) {
		if (d1[i] != d2[i])
			return false;
	}
	return true;
}

jv_method *get_method(java_lang_Class *cls, const utf8_const *name,
                      const utf8_const *signature)
{
	for (int16_t m = 0, n = cls->method_count; m < n; ++m) {
		jv_method *method = &cls->methods[m];
		if (utf8_consts_equal(method->name, name)
		    && utf8_consts_equal(method->signature, signature))
			return method;
	}
	return NULL;
}

void _Jv_InitClass(java_lang_Class *cls)
{
	// TODO: this probably fails for recursive dependent inits

	if (__builtin_expect (cls->state == JV_STATE_DONE, true))
		return;
	cls->state = JV_STATE_DONE;
	java_lang_Class *superclass = cls->superclass;
	if (superclass != NULL)
		_Jv_InitClass(superclass);
	// search class init method
	jv_method *init = get_method(cls, &clinit_name, &void_sig);
	if (init != NULL) {
		void(*initmethod)(void) = init->code;
		initmethod();
	}
}

jobject _Jv_NewPrimArray(java_lang_Class *eltype, jint count)
{
	(void)eltype;
	(void)count;
	fprintf(stderr, "TODO: Jv_NewPrimArray\n");
	return 0;
}
