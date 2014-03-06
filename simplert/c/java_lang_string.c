#include "types.h"

#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#include "debug.h"

// String rtti
extern java_lang_Class _ZN4java4lang6String6class$E;

java_lang_String *new_string(jchar *data, jint boffset, jint count)
{
	java_lang_String *result =
		(java_lang_String*)_Jv_AllocObjectNoFinalizer(&_ZN4java4lang6String6class$E);
	result->data    = data;
	result->boffset = boffset;
	result->count   = count;
	return result;
}

static const jchar *get_string_begin(const java_lang_String *string)
{
	return (const jchar*)((const char*)string->data + string->boffset);
}

jint _ZN4java4lang6String8hashCodeEJiv(const java_lang_String *this_)
{
	const jchar *begin = get_string_begin(this_);
	const jchar *end   = begin + this_->count;

	// djb-style hash
	jint hash = 5381;
	for (const jchar *c = begin; c < end; ++c) {
		hash = ((hash << 5) + hash) + *c;
	}

	return hash;
}

jboolean _ZN4java4lang6String6equalsEJbPNS0_6ObjectE(
	const java_lang_String *this_,
	const java_lang_Object *object)
{
	/* string is final so we can just compare vptrs to see if other is a
	 * string too */
	if (this_->base.vptr != object->vptr)
		return false;

	const java_lang_String *other = (const java_lang_String*)object;

	jint count       = this_->count;
	jint other_count = other->count;
	if (count != other_count)
		return false;

	const jchar *begin  = get_string_begin(this_);
	const jchar *obegin = get_string_begin(other);

	for (jint i = 0; i < count; ++i) {
		if (begin[i] != obegin[i])
			return false;
	}
	return true;
}

jchar _ZN4java4lang6String6charAtEJwi(const java_lang_String *this_, jint index)
{
	const jchar *chars = get_string_begin(this_);
	return chars[index];
}

void _ZN4java4lang6String4initEJvP6JArrayIwEiib(java_lang_String *this_,
	jarray chars, jint offset, jint count, jboolean dont_copy)
{
	jchar *data = malloc(count * sizeof(data[0]));
	if (data == NULL) {
		fprintf(stderr, "out of memory\n");
		abort();
	}
	const jchar *chars_data = (const jchar*)get_array_data(chars) + offset;
	if (chars->length < count) {
		fprintf(stderr, "chars array too short\n");
		abort();
	}
	(void)dont_copy;
	memcpy(data, chars_data, count * sizeof(data[0]));

	this_->data = data;
	this_->boffset = 0;
	this_->count   = count;
}

java_lang_String *string_from_c_chars(const char *chars, size_t len)
{
	jchar *data = malloc(len * sizeof(data[0]));
	if (data == NULL) {
		fprintf(stderr, "out of memory\n");
		abort();
	}
	// TODO: proper UTF-8 decoder...
	for (size_t i = 0; i < len; ++i) {
		data[i] = chars[i];
	}

	assert((size_t)(jint)len == len);
	return new_string(data, 0, len);
}

java_lang_String *_Z22_Jv_NewStringUtf8ConstP13_Jv_Utf8Const(const utf8_const *cnst)
{
	return string_from_c_chars(cnst->data, cnst->len);
}

void _ZN4java4lang6String8getCharsEJviiP6JArrayIwEi(
	const java_lang_String *this_, jint srcBegin, jint srcEnd,
	jarray dstArray, jint dstBegin)
{
	assert(srcBegin >= 0);
	assert(srcEnd >= srcBegin);
	assert(dstBegin >= 0);
	jint len = srcEnd - srcBegin;
	assert(len <= (dstArray->length - dstBegin));

	jchar       *dst  = (jchar*)get_array_data(dstArray) + dstBegin;
	const jchar *data = get_string_begin(this_) + srcBegin;

	memcpy(dst, data, len * sizeof(dst[0]));
}

java_lang_String *_ZN4java4lang6String9substringEJPS1_ii(const java_lang_String *this_, jint begin, jint end)
{
	assert(begin >= 0);
	assert(begin <= end);
	assert(end <= this_->count);

	return new_string(this_->data, this_->boffset + begin * sizeof(this_->data[0]), end - begin);
}

java_lang_String *_ZN4java4lang6String6concatEJPS1_S2_(const java_lang_String *this_, const java_lang_String *other)
{
	const jchar *src1 = get_string_begin(this_);
	const jchar *src2 = get_string_begin(other);
	jint len = this_->count + other->count;
	// TODO: check overflow...
	jchar *resdata = malloc(len * sizeof(resdata[0]));
	if (resdata == NULL) {
		fprintf(stderr, "out of memory\n");
		abort();
	}
	memcpy(resdata, src1, this_->count * sizeof(resdata[0]));
	memcpy(resdata + this_->count, src2, other->count * sizeof(resdata[0]));
	return new_string(resdata, 0, len);
}
