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
	/* Initialize runtime */
	init_prim_rtti();

	jv_method *mainm = get_method(cls, &main_name, &main_sig);
	if (mainm == NULL) {
		fprintf(stderr, "No main method found\n");
		exit(1);
	}
	void (*mainmethod)(jarray) = mainm->code;

	/* Construct array with commandline arguments */
	/* Skip program name */
	const int    java_argc = argc - 1;
	const char **java_argv = &argv[1];

	java_lang_Class   *string    = &_ZN4java4lang6String6class$E;
	jarray             args      = _Jv_NewObjectArray(java_argc, string, NULL);
	java_lang_String **data      = get_array_data(java_lang_String*, args);
	for (int i = 0; i < java_argc; ++i) {
		const char       *arg    = java_argv[i];
		size_t            len    = strlen(arg);
		java_lang_String *string = string_from_c_chars(arg, len);
		data[i] = string;
	}

	mainmethod(args);
}
