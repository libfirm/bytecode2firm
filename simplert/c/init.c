#include "types.h"
#include <stdio.h>

void _Jv_InitClass(jclass cls)
{
	(void)cls;
}

jobject _Jv_NewPrimArray(jclass eltype, jint count)
{
	(void)eltype;
	(void)count;
	fprintf(stderr, "TODO: Jv_NewPrimArray\n");
	return 0;
}
