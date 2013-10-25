#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "prims.h"
#include "types.h"

void __attribute__((unused)) print_utf8const(const utf8_const *utf8const)
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
	jv_method *main = NULL;
	for (size_t m = 0; m < klass->method_count; ++m) {
		jv_method *method = &klass->methods[m];
		if (is_main(method->name))
			main = method;
	}
	if (main == NULL) {
		fprintf(stderr, "No main method found\n");
		exit(1);
	}
	void (*mainmethod)(void) = main->code;
	mainmethod();
}
