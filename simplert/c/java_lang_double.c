#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include "types.h"
#include "mprec.h"

/* convert double to string, taken from libgcj */
static java_lang_String *normal_dtoa(jdouble value, bool is_float)
{
	char buffer[50], result[50];
	int decpt, sign;

	_dtoa (value, 0, 20, &decpt, &sign, NULL, buffer, (int)is_float);

	value = fabs (value);

	char *s = buffer;
	char *d = result;

	if (sign)
		*d++ = '-';

	if ((value >= 1e-3 && value < 1e7) || value == 0) {
		if (decpt <= 0) {
			*d++ = '0';
		} else {
			for (int i = 0; i < decpt; i++) {
				if (*s)
					*d++ = *s++;
				else
					*d++ = '0';
			}
		}

		*d++ = '.';

		if (*s == 0) {
			*d++ = '0';
			decpt++;
		}

		while (decpt++ < 0)
			*d++ = '0';

		while (*s)
			*d++ = *s++;

		*d = 0;

		return string_from_c_chars(result, strlen(result));
	}

	*d++ = *s++;
	decpt--;
	*d++ = '.';

	if (*s == 0)
		*d++ = '0';

	while (*s)
		*d++ = *s++;

	*d++ = 'E';

	if (decpt < 0) {
		*d++ = '-';
		decpt = -decpt;
	}

	char exp[4];
	char *e = exp + sizeof exp;

	*--e = 0;
	do {
		*--e = '0' + decpt % 10;
		decpt /= 10;
	} while (decpt > 0);

	while (*e) {
		*d++ = *e++;
	}

	*d = 0;

	return string_from_c_chars(result, strlen(result));
}

java_lang_String *double_to_string(jdouble value, bool is_float)
{
	const char *chars;
	switch (fpclassify(value)) {
	case FP_NAN:
		chars = "NaN";
		break;
	case FP_ZERO:
		chars = "0.0";
		break;
	case FP_INFINITE:
		chars = signbit(value) ? "-Infinity" : "Infinity";
		break;
	default:
		return normal_dtoa(value, is_float);
	}
	return string_from_c_chars(chars, strlen(chars));
}

java_lang_String *_ZN4java4lang6Double8toStringEJPNS0_6StringEd(jdouble value)
{
	return double_to_string(value, false);
}
