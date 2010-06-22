#include "mangle.h"

#include <string.h>
#include <assert.h>
#include "adt/obst.h"
#include "adt/error.h"
#include "types.h"

static struct obstack obst;

/**
 * Hint: you must emit "N" before and "E" after a call to this method.
 */
static void mangle_qualified_class_name(ident *class_ident)
{
	const char *string      = get_id_str(class_ident);
	const char *p           = string;
	while (*p != '\0') {
		while (*p == '/' || *p == '$')
			++p;
		/* search for '/' or '\0' */
		size_t l;
		for (l = 0; p[l] != '\0' && p[l] != '/' && p[l] != '$'; ++l) {
		}
		obstack_printf(&obst, "%d", l);
		for ( ; l > 0; --l) {
			obstack_1grow(&obst, *(p++));
		}
	}
}

static void mangle_type(ir_type *type)
{
	if (is_Primitive_type(type)) {
		const char *tag = get_type_link(type);
		size_t      len = strlen(tag);
		obstack_grow(&obst, tag, len);
	} else if (is_Pointer_type(type)) {
		ir_type *pointsto = get_pointer_points_to_type(type);
		if (is_Class_type(pointsto)) {
			ident *class_ident = get_class_ident(pointsto);
			obstack_grow(&obst, "PN", 2);
			mangle_qualified_class_name(class_ident);
			obstack_1grow(&obst, 'E');
		} else {
			// assume it's an array
			obstack_1grow(&obst, 'P');
			mangle_type(pointsto);
		}
	}
}

/**
 *  mangles in a C++ like fashion so we can use c++filt to demangle
 */
ident *mangle_entity_name(ir_entity *entity, ident *id)
{
	assert (obstack_object_size(&obst) == 0);
	assert (entity != NULL);

	ir_type *owner = get_entity_owner(entity);
	ir_type *type  = get_entity_type(entity);

	obstack_grow(&obst, "_ZN", 3);

	ident *class_ident = get_class_ident(owner);
	mangle_qualified_class_name(class_ident);

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

	/* mangle return type */
	int n_ress = get_method_n_ress(type);
	if (n_ress == 0) {
		obstack_1grow(&obst, 'v');
	} else {
		assert(n_ress == 1);
		mangle_type(get_method_res_type(type, 0));
	}

	/* mangle parameter types */
	int n_params = get_method_n_params(type);
	int start    = get_entity_allocation(entity) == allocation_static ? 0 : 1;
	if (n_params-start == 0) {
		obstack_1grow(&obst, 'v');
	} else {
		for (int i = start; i < n_params; ++i) {
			ir_type *parameter = get_method_param_type(type, i);
			mangle_type(parameter);
		}
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

	obstack_grow(&obst, "_ZTVN", 5);

	ident      *class_ident = get_class_ident(clazz);
	mangle_qualified_class_name(class_ident);

	obstack_1grow(&obst, 'E');

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
