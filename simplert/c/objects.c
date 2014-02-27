#include "types.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include "debug.h"

jobject _Jv_AllocObjectNoFinalizer(java_lang_Class *type)
{
	size_t size = type->size_in_bytes;
	jobject result = calloc(1, size);
	if (result == 0) {
		fprintf(stderr, "panic: out of memory\n");
		abort();
	}
	result->vptr = type->vtable;
	return result;
}

typedef struct array_header_t {
	java_lang_Object base;
	jint             length;
} array_header_t;

java_lang_Class __attribute__((weak)) _ZN4java4lang6Object6class$E;
vtable_t __attribute__((weak)) _ZTVN4java4lang6ObjectE = {
	&_ZN4java4lang6Object6class$E
};

jobject _Jv_NewPrimArray(java_lang_Class *eltype, jint count)
{
	if (__builtin_expect (count < 0, false)) {
		fprintf(stderr, "throw negative array size\n");
		abort();
	}

	assert(eltype->vtable == (vtable_t*)-1);
	int elsize = eltype->size_in_bytes;
	// TODO: check for overflow
	size_t size = sizeof(array_header_t) + elsize * count;

	array_header_t *result = calloc(1, size);
	// TODO: create a better vtable/object than java.lang.Object
	result->base.vptr = &_ZTVN4java4lang6ObjectE;
	result->length    = count;
	return (jobject)result;
}
