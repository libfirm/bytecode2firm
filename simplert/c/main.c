#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "prims.h"
#include "types.h"

void print_utf8const(const utf8_const *utf8const)
{
	for (size_t c = 0; c < utf8const->len; ++c) {
		putchar(utf8const->data[c]);
	}
}

bool is_main(const utf8_const *utf8const)
{
	return utf8const->len == 4 && utf8const->data[0] == 'm'
	    && utf8const->data[1] == 'a' && utf8const->data[2] == 'i'
	    && utf8const->data[3] == 'n';
}

void JvRunMain(jclass klass, int argc, const char **argv)
{
	bool simplert_dump_infos = false;
	for (int i = 0; i < argc; ++i) {
		if (strcmp(argv[i], "--simplert-dump-infos") == 0) {
			simplert_dump_infos = true;
			break;
		}
	}

	if (simplert_dump_infos) {
		printf("Main class: ");
		print_utf8const(klass->name);
		putchar('\n');
		for (int16_t f = 0; f < klass->field_count; ++f) {
			jv_field *field = &klass->fields[f];
			printf(" * Field ");
			print_utf8const(field->name);
			putchar('\n');
		}
	}

	jv_method *mainm = NULL;
	for (int16_t m = 0; m < klass->method_count; ++m) {
		jv_method *method = &klass->methods[m];
		if (simplert_dump_infos) {
			printf(" * Method ");
			print_utf8const(method->name);
			putchar(' ');
			print_utf8const(method->signature);
			putchar('\n');
		}
		if (is_main(method->name))
			mainm = method;
	}
	if (mainm == NULL) {
		fprintf(stderr, "No main method found\n");
		exit(1);
	}
	void (*mainmethod)(void) = mainm->code;
	mainmethod();
}
