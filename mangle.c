#include "mangle.h"

#include <string.h>
#include <assert.h>
#include "adt/obst.h"
#include "adt/error.h"
#include "types.h"

static struct obstack obst;

static void mangle_type(ir_type *type)
{
	if (is_Primitive_type(type)) {
		const char *tag = get_type_link(type);
		size_t      len = strlen(tag);
		obstack_grow(&obst, tag, len);
	} else {
		/* TODO */
	}
}

static void mangle_java_ident(ident *ident)
{
	const char *name = get_id_str(ident);
	for (const char *p = name; *p != '\0'; ++p) {
		char c = *p;
		if (c == '/')
			c = '_';
		obstack_1grow(&obst, c);
	}
}

/**
 * mangle method type in the same fashion as edgjfe/jack
 */
static void mangle_method_type_simple(ir_type *type)
{
	if (is_Pointer_type(type)) {
		ir_type *pointsto = get_pointer_points_to_type(type);
		assert(is_Class_type(pointsto));
		obstack_1grow(&obst, 'L');
		mangle_java_ident(get_class_ident(pointsto));
		obstack_grow(&obst, "_2", 2);
	}
	else if (type == type_byte)    { obstack_1grow(&obst, 'B'); }
	else if (type == type_char)    { obstack_1grow(&obst, 'C'); }
	else if (type == type_short)   { obstack_1grow(&obst, 'S'); }
	else if (type == type_int)     { obstack_1grow(&obst, 'I'); }
	else if (type == type_long)    { obstack_1grow(&obst, 'J'); }
	else if (type == type_boolean) { obstack_1grow(&obst, 'Z'); }
	else if (type == type_float)   { obstack_1grow(&obst, 'F'); }
	else if (type == type_double)  { obstack_1grow(&obst, 'D'); }
	else if (type == type_array_byte_boolean) { obstack_grow(&obst, "_3B", 3); }
	else if (type == type_array_char)         { obstack_grow(&obst, "_3C", 3); }
	else if (type == type_array_short)        { obstack_grow(&obst, "_3S", 3); }
	else if (type == type_array_int)          { obstack_grow(&obst, "_3I", 3); }
	else if (type == type_array_long)         { obstack_grow(&obst, "_3J", 3); }
	else if (type == type_array_float)        { obstack_grow(&obst, "_3F", 3); }
	else if (type == type_array_double)       { obstack_grow(&obst, "_3D", 3); }
	else if (type == type_array_reference)    { obstack_grow(&obst, "_3R", 3); }
	else {
		/* TODO */
		panic("TODO: can't mangle type");
	}
}

ident *mangle_native_func(ir_type *owner, ir_type *type, ident *id)
{
	assert(obstack_object_size(&obst) == 0);
	obstack_grow(&obst, "Java_", 5);

	/* mangle class name */
	mangle_java_ident(get_class_ident(owner));
	obstack_1grow(&obst, '_');
	/* mangle entity name */
	size_t      len    = get_id_strlen(id);
	const char *string = get_id_str(id);
	obstack_grow(&obst, string, len);

	if (!is_Method_type(type))
		goto name_finished;

	obstack_grow(&obst, "__", 2);

	assert(is_Method_type(type));
	int n_params = get_method_n_params(type);
	if (n_params == 0) {
		obstack_1grow(&obst, 'V');
	} else {
		for (int p = 0; p < n_params; ++p) {
			ir_type *parameter_type = get_method_param_type(type, p);
			mangle_method_type_simple(parameter_type);
		}
	}

name_finished: ;
	size_t  result_len    = obstack_object_size(&obst);
	char   *result_string = obstack_finish(&obst);
	ident  *result        = new_id_from_chars(result_string, result_len);
	obstack_free(&obst, result_string);

	return result;
}

ident *mangle_entity_name(ir_type *owner, ir_type *type, ident *id)
{
	assert(obstack_object_size(&obst) == 0);

	if (owner != NULL) {
		/* mangle in a C++ like fashion so we can use c++filt to demangle */
		obstack_grow(&obst, "_ZN", 3);

		/* mangle class name */
		ident      *class_ident = get_class_ident(owner);
		const char *string      = get_id_str(class_ident);
		const char *p           = string;
		while (*p != '\0') {
			while (*p == '/')
				++p;
			/* search for '/' or '\0' */
			size_t l;
			for (l = 0; p[l] != '\0' && p[l] != '/'; ++l) {
			}
			obstack_printf(&obst, "%d", l);
			for ( ; l > 0; --l) {
				obstack_1grow(&obst, *(p++));
			}
		}
	}

	/* mangle entity name */
	const char *string = get_id_str(id);
	size_t      len    = get_id_strlen(id);
	if (strcmp(string, "<init>") == 0) {
		obstack_grow(&obst, "C1", 2);
	} else {
		obstack_printf(&obst, "%d%s", (int) len, string);
	}
	obstack_1grow(&obst, 'E');

	if (!is_Method_type(type))
		goto name_finished;

	obstack_1grow(&obst, 'J');

	/* mangle parameter types */
	int n_params = get_method_n_params(type);
	for (int i = 0; i < n_params; ++i) {
		ir_type *parameter = get_method_param_type(type, i);
		mangle_type(parameter);
	}

	/* mangle return type */
	if (get_method_n_ress(type) == 0) {
		obstack_1grow(&obst, 'v');
	} else {
		assert(get_method_n_ress(type) == 1);
		mangle_type(get_method_res_type(type, 0));
	}

name_finished: ;
	size_t  result_len    = obstack_object_size(&obst);
	char   *result_string = obstack_finish(&obst);
	ident  *result        = new_id_from_chars(result_string, result_len);
	obstack_free(&obst, result_string);

	return result;
}

ident *mangle_vtable_name(ir_type *clazz)
{
	assert(obstack_object_size(&obst) == 0);
	assert(clazz != NULL && is_Class_type(clazz));

	obstack_grow(&obst, "vtable_", 7);
	/* mangle in a C++ like fashion so we can use c++filt to demangle */
	obstack_grow(&obst, "_ZN", 3);

	/* mangle class name */
	ident      *class_ident = get_class_ident(clazz);
	const char *string      = get_id_str(class_ident);
	const char *p           = string;
	while (*p != '\0') {
		while (*p == '/')
			++p;
		/* search for '/' or '\0' */
		size_t l;
		for (l = 0; p[l] != '\0' && p[l] != '/'; ++l) {
		}
		obstack_printf(&obst, "%d", l);
		for ( ; l > 0; --l) {
			obstack_1grow(&obst, *(p++));
		}
	}

	size_t  result_len    = obstack_object_size(&obst);
	char   *result_string = obstack_finish(&obst);
	ident  *result        = new_id_from_chars(result_string, result_len);
	obstack_free(&obst, result_string);

	return result;
}

void init_mangle(void)
{
	obstack_init(&obst);

	set_type_link(type_byte, "c");
	set_type_link(type_char, "w");
	set_type_link(type_short, "s");
	set_type_link(type_int, "i");
	set_type_link(type_long, "x");
	set_type_link(type_boolean, "b");
	set_type_link(type_float, "f");
	set_type_link(type_double, "d");
}

void deinit_mangle(void)
{
	obstack_free(&obst, NULL);
}
