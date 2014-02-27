#include "types.h"

#include <stdlib.h>
#include <stdio.h>

#include "debug.h"

jobject _Jv_AllocObjectNoFinalizer(java_lang_Class *type)
{
	size_t size = type->size_in_bytes;
	jobject result = calloc(1, size);
	result->vptr = type->vtable;
	return result;
}
