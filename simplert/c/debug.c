#include "debug.h"

#include <stdio.h>

static void print_utf8const(const utf8_const *utf8const)
{
	for (size_t c = 0; c < utf8const->len; ++c) {
		putchar(utf8const->data[c]);
	}
}

void dump_rtti(const java_lang_Class *cls)
{
	printf("RTTI of ");
	print_utf8const(cls->name);
	printf(" size: %d flags: 0x%X\n", cls->size_in_bytes, cls->accflags);
	for (int16_t f = 0; f < cls->field_count; ++f) {
		jv_field *field = &cls->fields[f];
		printf(" * Field ");
		print_utf8const(field->name);
		putchar('\n');
	}

	for (int16_t m = 0; m < cls->method_count; ++m) {
		jv_method *method = &cls->methods[m];
		printf(" * Method ");
		print_utf8const(method->name);
		putchar(' ');
		print_utf8const(method->signature);
		putchar('\n');
	}
}
