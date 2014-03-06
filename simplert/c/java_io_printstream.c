#include "types.h"

#include <stdio.h>

void _ZN4java2io11PrintStream7putcharEJvw(jchar c)
{
	putchar(c);
}

void _ZN4java2io11PrintStream5printEJvi(jobject this_, jint i)
{
	(void)this_;
	printf("%d", i);
}
