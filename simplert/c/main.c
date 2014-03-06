#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "types.h"
#include "debug.h"

static const utf8_const main_name = { 0x05b9,  4, { "main"                   } };
static const utf8_const main_sig  = { 0xe82a, 22, { "([Ljava.lang.String;)V" } };

extern java_lang_Class _ZN4java4lang6String6class$E;

void JvRunMain(java_lang_Class *cls, int argc, const char **argv)
{
	// initialize runtime
	init_prim_rtti();

	jv_method *mainm = get_method(cls, &main_name, &main_sig);
	if (mainm == NULL) {
		fprintf(stderr, "No main method found\n");
		exit(1);
	}
	void (*mainmethod)(jarray) = mainm->code;

	// construct array with commandline arguments
	java_lang_Class *string = &_ZN4java4lang6String6class$E;
	jarray           args   = _Jv_NewObjectArray(argc, string, NULL);
	java_lang_String **data = (java_lang_String**)get_array_data(args);
	for (int i = 0; i < argc; ++i) {
		const char       *arg    = argv[i];
		size_t            len    = strlen(arg);
		java_lang_String *string = string_from_c_chars(arg, len);
		data[i] = string;
	}

	mainmethod(args);
}
