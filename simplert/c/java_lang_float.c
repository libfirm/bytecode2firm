#include <stdio.h>
#include <string.h>

#include "types.h"

java_lang_String *_ZN4java4lang5Float8toStringEJPNS0_6StringEf(jfloat value)
{
	return double_to_string(value, true);
}
