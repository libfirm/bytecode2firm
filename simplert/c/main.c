#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "types.h"
#include "debug.h"

static const utf8_const main_name = { 0x05b9,  4, "main"                   };
static const utf8_const main_sig  = { 0xe82a, 22, "([Ljava.lang.String;)V" };

void JvRunMain(java_lang_Class *cls, int argc, const char **argv)
{
	// initialize runtime
	init_prim_rtti();

	// search main method (and maybe dump some infos)
	bool simplert_dump_infos = false;
	for (int i = 0; i < argc; ++i) {
		if (strcmp(argv[i], "--simplert-dump-infos") == 0) {
			simplert_dump_infos = true;
			break;
		}
	}

	if (simplert_dump_infos) {
		dump_rtti(cls);
	}

	jv_method *mainm = get_method(cls, &main_name, &main_sig);
	if (mainm == NULL) {
		fprintf(stderr, "No main method found\n");
		exit(1);
	}
	void (*mainmethod)(void) = mainm->code;
	mainmethod();
}
