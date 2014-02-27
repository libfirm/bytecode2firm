#include "types.h"

#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

extern java_lang_Class _ZN4java4lang6String6class$E;

static const char **get_data_field(jobject string)
{
	size_t offset = sizeof(java_lang_Object);
	return (const char**)((char*)string + offset);
}

static jint *get_boffset_field(jobject string)
{
	size_t offset = sizeof(java_lang_Object) + sizeof(char*);
	return (jint*)((char*)string + offset);
}

static jint *get_count_field(jobject string)
{
	size_t offset = sizeof(java_lang_Object) + sizeof(char*) + sizeof(jint);
	return (jint*)((char*)string + offset);
}

jint _ZN4java4lang6String8hashCodeEJiv(jobject this_)
{
	const char *begin = *(get_data_field(this_)) + *(get_boffset_field(this_));
	const char *end   = begin + *(get_count_field(this_));

	// djb-style hash
	jint hash = 5381;
	for (const char *c = begin; c <= end; ++c) {
		hash = ((hash << 5) + hash) + *c;
	}

	return hash;
}

jboolean _ZN4java4lang6String6equalsEJbPNS0_6ObjectE(jobject this_,
	jobject other)
{
	/* string is final so we can just compare vptrs to see if other is a
	 * string too */
	if (this_->vptr != other->vptr)
		return false;

	jint count       = *(get_count_field(this_));
	jint other_count = *(get_count_field(other));
	if (count != other_count)
		return false;

	const char *begin  = *(get_data_field(this_)) + *(get_boffset_field(this_));
	const char *obegin = *(get_data_field(other)) + *(get_boffset_field(other));

	for (jint i = 0; i < count; ++i) {
		if (begin[i] != obegin[i])
			return false;
	}
	return true;
}

jchar _ZN4java4lang6String6charAtEJwi(jobject this_, jint index)
{
	const char *chars = *(get_data_field(this_));
	jint offset = *(get_boffset_field(this_));
	return chars[offset + index];
}

void _ZN4java4lang6String4initEJvP6JArrayIcEiii(jobject this_, jarray chars,
	jint hibyte, jint offset, jint count)
{
	*(get_boffset_field(this_)) = offset;
	*(get_count_field(this_)) = count;
	(void)chars; // TODO
	(void)hibyte;
}

void _ZN4java4lang6String4initEJvP6JArrayIwEiib(jobject this_, jarray chars,
	jint offset, jint count, jboolean dont_copy)
{
	*(get_boffset_field(this_)) = offset;
	*(get_count_field(this_)) = count;
	(void)chars; // TODO
	(void)dont_copy;
}

jobject _Z22_Jv_NewStringUtf8ConstP13_Jv_Utf8Const(const utf8_const *cnst)
{
	jobject str = _Jv_AllocObjectNoFinalizer(&_ZN4java4lang6String6class$E);
	*(get_data_field(str))    = cnst->data;
	*(get_boffset_field(str)) = 0;
	*(get_count_field(str))   = cnst->len;
	return str;
}
