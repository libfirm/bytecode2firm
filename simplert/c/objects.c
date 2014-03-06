#include "types.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

#include "debug.h"

jobject _Jv_AllocObjectNoFinalizer(java_lang_Class *type)
{
	jint size = type->size_in_bytes;
	assert((size_t)size >= sizeof(java_lang_Object));
	jobject result = calloc(1, size);
	if (result == 0) {
		fprintf(stderr, "panic: out of memory\n");
		abort();
	}
	result->vptr = type->vtable;
	return result;
}

extern java_lang_Class _ZN4java4lang6Object6class$E;
extern extended_vtable_t _ZTVN4java4lang6ObjectE;
extern extended_vtable_t _ZTVN4java4lang5ClassE;

static vtable_t *duplicate_object_vtable(void)
{
	const vtable_t *obj_table      = &_ZTVN4java4lang6ObjectE.vtable;
	unsigned        vtable_count   = obj_table->rtti->vtable_method_count;
	size_t          obj_table_size = sizeof(vtable_t) + vtable_count*sizeof(void*);
	vtable_t *vtable = calloc(1, obj_table_size);
	memcpy(vtable, obj_table, obj_table_size);
	return vtable;
}

static java_lang_Class *create_array_class(java_lang_Class *eltype)
{
	java_lang_Class *arrayclass = calloc(1, sizeof(java_lang_Class));
	vtable_t *vtable = duplicate_object_vtable();
	vtable->rtti = arrayclass;

	size_t max_name_len;
	if (_ZN4java4lang5Class11isPrimitiveEJbv(eltype)) {
		max_name_len = 2;
	} else {
		max_name_len = eltype->name->len + 3;
	}

	utf8_const *namecnst = calloc(1, sizeof(utf8_const) + max_name_len);
	char *name_chars = namecnst->data;
	size_t idx = 0;
	name_chars[idx++] = '[';

	if (_ZN4java4lang5Class11isPrimitiveEJbv(eltype)) {
		name_chars[idx++] = (char)eltype->method_count;
	} else {
		const char *elname = eltype->name->data;
		if (elname[0] != '[')
			name_chars[idx++] = 'L';
		memcpy(&name_chars[idx], elname, eltype->name->len);
		idx += eltype->name->len;
		if (elname[0] != '[')
			name_chars[idx++] = ';';
	}
	assert(idx <= max_name_len);
	namecnst->len = idx;
	namecnst->hash = calc_string_hash(name_chars, idx);

	arrayclass->base.vptr       = &_ZTVN4java4lang5ClassE.vtable;
	arrayclass->vtable          = vtable;
	arrayclass->size_in_bytes   = -1;
	arrayclass->me.element_type = eltype;
	arrayclass->state           = JV_STATE_DONE;
	arrayclass->name            = namecnst;

	return arrayclass;
}

static java_lang_Class *get_array_class(java_lang_Class *eltype)
{
	if (eltype->arrayclass != NULL)
		return eltype->arrayclass;

	java_lang_Class *arrayclass = create_array_class(eltype);
	eltype->arrayclass = arrayclass;
	return arrayclass;
}

jarray _Jv_NewPrimArray(java_lang_Class *eltype, jint count)
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
	result->base.vptr = get_array_class(eltype)->vtable;
	result->length    = count;
	return result;
}

jarray _Jv_NewObjectArray(jsize count, java_lang_Class *eltype, jobject init)
{
	(void)eltype;
	if (__builtin_expect (count < 0, false)) {
		fprintf(stderr, "throw negative array size\n");
		abort();
	}

	size_t elsize = sizeof(java_lang_Object*);
	// TODO: check for overflow
	size_t size = sizeof(array_header_t) + elsize * count;

	array_header_t *result = calloc(1, size);
	result->base.vptr = get_array_class(eltype)->vtable;
	result->length    = count;

	java_lang_Object **data = (java_lang_Object**)get_array_data(result);
	for (jsize i = 0; i < count; ++i) {
		data[i] = init;
	}
	return result;
}

jarray _Z17_Jv_NewMultiArrayPN4java4lang5ClassEiPi(java_lang_Class *type,
	jint n_dims, jint *sizes)
{
	assert(_ZN4java4lang5Class7isArrayEJbv(type));
	java_lang_Class *eltype = type->me.element_type;

	array_header_t *result;
	if (_ZN4java4lang5Class11isPrimitiveEJbv(eltype)) {
		assert(n_dims == 1);
		result = _Jv_NewPrimArray(eltype, sizes[0]);
	} else {
		result = _Jv_NewObjectArray(sizes[0], eltype, NULL);
	}

	if (n_dims > 1) {
		jarray *contents = (jarray*)get_array_data(result);
		for (jint i = 0; i < sizes[0]; ++i) {
			contents[i]
				= _Z17_Jv_NewMultiArrayPN4java4lang5ClassEiPi(eltype, n_dims-1,
				                                              sizes + 1);
		}
	}

	return result;
}

java_lang_Class *_Z17_Jv_GetArrayClassPN4java4lang5ClassEPNS0_11ClassLoaderE(
	java_lang_Class *eltype, jobject loader)
{
	(void)loader;
	return get_array_class(eltype);
}

jint _ZN4java4lang6Object8hashCodeEJiv(jobject this_)
{
	return (jint)(((intptr_t)this_) >> 3);
}

java_lang_Class *_ZN4java4lang6Object8getClassEJPNS0_5ClassEv(jobject this_)
{
	return this_->vptr->rtti;
}

static bool subclass(java_lang_Class *cls1, java_lang_Class *cls2)
{
	if (cls1 == cls2)
		return true;
	/* check interfaces */
	for (int i = 0; i < cls1->interface_count; ++i) {
		java_lang_Class *iface = cls1->interfaces[i];
		if (subclass(iface, cls2))
			return true;
	}
	/* check superclass */
	java_lang_Class *super = cls1->superclass;
	if (super != NULL)
		return subclass(super, cls2);
	else
		return false;
}

jboolean _Jv_IsInstanceOf(jobject obj, java_lang_Class *cls)
{
	if (obj == NULL)
		return true;
	return subclass(obj->vptr->rtti, cls);
}

int _Jv_CheckCast(java_lang_Class *cls, jobject obj)
{
	if (!_Jv_IsInstanceOf(obj, cls)) {
		abort();
	}
	return true;
}

static bool utf8_consts_equal(const utf8_const *c1, const utf8_const *c2)
{
	if (c1->hash != c2->hash)
		return false;
	uint16_t len = c1->len;
	if (len != c2->len)
		return false;
	const char *d1 = c1->data;
	const char *d2 = c2->data;
	for (uint16_t i = 0; i < len; ++i) {
		if (d1[i] != d2[i])
			return false;
	}
	return true;
}

jv_method *get_method(java_lang_Class *cls, const utf8_const *name,
                      const utf8_const *signature)
{
	for (int16_t m = 0, n = cls->method_count; m < n; ++m) {
		jv_method *method = &cls->me.methods[m];
		if (utf8_consts_equal(method->name, name)
		    && utf8_consts_equal(method->signature, signature))
			return method;
	}
	java_lang_Class *superclass = cls->superclass;
	if (superclass != NULL)
		return get_method(superclass, name, signature);
	else
		return NULL;
}

void *_Jv_LookupInterfaceMethod(java_lang_Class *cls, const utf8_const *name,
                                const utf8_const *sig)
{
	return get_method(cls, name, sig)->code;
}

void _Jv_ThrowAbstractMethodError(void)
{
	fprintf(stderr, "panic: abstract method called\n");
	abort();
}
