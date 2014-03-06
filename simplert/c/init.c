#include "types.h"

#include <stdio.h>
#include <stdbool.h>

static const utf8_const clinit_name = { 0x0ea9, 8, { "<clinit>" } };
static const utf8_const void_sig    = { 0x9b75, 3, { "()V"      } };

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
