#include "mangle.h"

#include <string.h>
#include <assert.h>
#include "adt/obst.h"
#include "adt/error.h"
#include "types.h"

static struct obstack obst;

#define CT_SIZE 36 // FIXME: 36 entries ought to be enough for anybody..
typedef struct {
	char *name;
} cte_entry;

/*
 * The compression table contains prefixes of currently mangled name.
 * Pointers to and arrays of a specific type, and the JArray keyword, cause an additional entry.
 * Example: mangling
 * JArray<java::lang::Object*>* java::lang::ClassLoader::putDeclaredAnnotations(java::lang::Class*, int, int, int, JArray<java::lang::Object*>*)
 * S_  = java
 * S0_ = java/lang
 * S1_ = java/lang/ClassLoader
 * S2_ = JArray
 * S3_ = java/lang/Object
 * S4_ = Pjava/lang/Object
 * S5_ = JArray<Pjava/lang/Object>
 * S6_ = PJArray<Pjava/lang/Object>
 * => _ZN4java4lang11ClassLoader22putDeclaredAnnotationsEJP6JArrayIPNS0_6ObjectEEPNS0_5ClassEiiiS6_
 */
static cte_entry ct[CT_SIZE];
static int next_ct_entry;

static const char* base36 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static char mangle_buffer[1024];

extern char *strdup (__const char *__s); // FIXME: ??

static void flush_ct(void)
{
	for (int i = 0; i < CT_SIZE; i++) {
		if (ct[i].name != NULL)
			free(ct[i].name);
		ct[i].name = NULL;
	}
	next_ct_entry = 0;
}

static int find_in_ct(const char* name)
{
	for (int i = 0; i < next_ct_entry; i++) {
		if (strcmp(ct[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

static void insert_ct(const char* name)
{
	assert (next_ct_entry < CT_SIZE);
	ct[next_ct_entry].name = strdup(name);
	next_ct_entry++;
}

static void emit_substitution(int match, struct obstack *obst)
{
	obstack_1grow(obst, 'S');
	if (match > 0)
		obstack_1grow(obst, base36[match-1]);
	obstack_1grow(obst, '_');
}

static void mangle_type(ir_type *type, struct obstack *obst);

static void mangle_primitive_type(ir_type *type, struct obstack *obst)
{
	assert (is_Primitive_type(type));
	const char *tag = get_type_link(type);
	size_t      len = strlen(tag);
	obstack_grow(obst, tag, len);
}

static int mangle_qualified_class_name(ir_type *class_type, int is_pointer, struct obstack *obst)
{
	assert (is_Class_type(class_type));

	ident      *class_ident = get_class_ident(class_type);
	const char *string      = get_id_str(class_ident);
	const char *p           = string;
	size_t      slen        = strlen(string);
	int         emitted_N   = 0;

	int full_match          = find_in_ct(string);
	int full_match_p        = -1;

	if (is_pointer) {
		mangle_buffer[0] = 'P';
		strcpy(mangle_buffer+1, string);
		mangle_buffer[slen+1] = '\0';
		full_match_p = find_in_ct(mangle_buffer);
	}

	if (full_match_p >= 0) {
		// we already have the *class entry
		emit_substitution(full_match_p, obst);
		return emitted_N;
	}

	if (full_match >= 0) {
		if (is_pointer) {
			insert_ct(mangle_buffer); // we have the class entry -> use it and introduce the *class entry
			obstack_1grow(obst, 'P');
		}
		emit_substitution(full_match, obst);
		return emitted_N;
	}

	// no full match, we'll construct a new composite name
	if (is_pointer)
		obstack_1grow(obst, 'P');
	obstack_1grow(obst, 'N');
	emitted_N = 1;

	int last_match          = -1;
	while (*p != '\0') {
		while (*p == '/')
			++p;
		/* search for '/' or '\0' */
		size_t l;
		for (l = 0; p[l] != '\0' && p[l] != '/'; ++l) {
		}

		const char *comp_begin   = p;
		const char *comp_end     = p + l;
		unsigned    comp_end_idx = (comp_end-string);
		strncpy(mangle_buffer, string, comp_end_idx);
		mangle_buffer[comp_end_idx] = '\0';
		p = comp_end;

		int match = find_in_ct(mangle_buffer);
		if (match >= 0) {
			last_match = match;
		} else {
			insert_ct(mangle_buffer);

			if (last_match >= 0) {
				emit_substitution(last_match, obst);
				last_match = -1;
			}
			obstack_printf(obst, "%d", l);
			obstack_grow(obst, comp_begin, l);
		}
	}

	if (is_pointer) {
		// insert the *class entry AFTER the class entry (that has been created in the last loop iteration above)
		mangle_buffer[0] = 'P';
		strcpy(mangle_buffer+1, string);
		mangle_buffer[slen+1] = '\0';
		assert (find_in_ct(mangle_buffer) == -1);
		insert_ct(mangle_buffer);
	}

	return emitted_N;
}

static void mangle_type_without_substitition(ir_type *type, struct obstack *obst)
{
	if (is_Primitive_type(type)) {
		mangle_primitive_type(type, obst);
	} else if (is_Pointer_type(type)) {
		ir_type *pt = get_pointer_points_to_type(type);
		if (is_Class_type(pt)) {
			obstack_1grow(obst, 'P');
			obstack_printf(obst, "%s", get_class_name(pt));
		} else {
			obstack_grow(obst, "JArray<", 7);
			mangle_type_without_substitition(pt, obst);
			obstack_1grow(obst, '>');
		}
	}
}

static void mangle_array_type(ir_type *type, struct obstack *obst)
{
	struct obstack unsubstituted_obst;
	obstack_init(&unsubstituted_obst);
	obstack_grow(&unsubstituted_obst, "PJArray<", 8);
	mangle_type_without_substitition(type, &unsubstituted_obst);
	obstack_1grow(&unsubstituted_obst, '>');
	const char *unsubstituted = obstack_finish(&unsubstituted_obst);

	int full_match = find_in_ct(unsubstituted);
	if (full_match >= 0) {
		emit_substitution(full_match, obst);
	} else {
		obstack_1grow(obst, 'P');

		int jarray_match = find_in_ct("JArray");
		if (jarray_match >= 0) {
			emit_substitution(jarray_match, obst);
		} else {
			obstack_grow(obst, "6JArray", 7);
			insert_ct("JArray");
		}
		obstack_1grow(obst, 'I');

		mangle_type(type, obst);
		obstack_1grow(obst, 'E');

		insert_ct(unsubstituted+1); // insert the non-pointer version of the JArray.
		insert_ct(unsubstituted);
	}

	obstack_free(&unsubstituted_obst, NULL);
}

static void mangle_type(ir_type *type, struct obstack *obst)
{
	if (is_Primitive_type(type)) {
		mangle_primitive_type(type, obst);
	} else if (is_Pointer_type(type)) {
		ir_type *pointsto = get_pointer_points_to_type(type);
		if (is_Class_type(pointsto)) {
			int emitted_N = mangle_qualified_class_name(pointsto, 1, obst);
			if (emitted_N)
				obstack_1grow(obst, 'E');
		} else {
			// assume it's an array
			mangle_array_type(pointsto, obst);
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

	flush_ct();

	ir_type *owner = get_entity_owner(entity);
	ir_type *type  = get_entity_type(entity);

	obstack_grow(&obst, "_Z", 2);

	int emitted_N = mangle_qualified_class_name(owner, 0, &obst);
	assert (emitted_N);

	int is_ctor = 0;

	/* mangle entity name */
	const char *string = get_id_str(id);
	size_t      len    = get_id_strlen(id);
	if (strcmp(string, "<init>") == 0) {
		obstack_grow(&obst, "C1", 2);
		is_ctor = 1;
	} else if (strcmp(string, "<clinit>") == 0) {
		obstack_grow(&obst, "18__U3c_clinit__U3e_", 20);
	} else {
		obstack_printf(&obst, "%d%s", (int) len, string);

		if (strcmp(string, "not") == 0
		 || strcmp(string, "and") == 0
		 || strcmp(string, "or") == 0
		 || strcmp(string, "xor") == 0
		 || strcmp(string, "delete") == 0) {
			// FIXME: poor man's check for some c++ keywords
			obstack_1grow(&obst, '$');
		}
	}
	obstack_1grow(&obst, 'E');

	if (!is_Method_type(type))
		goto name_finished;

	int n_ress   = get_method_n_ress(type);
	int n_params = get_method_n_params(type);

	if (is_ctor && n_ress == 0 && n_params == 1) {
		obstack_1grow(&obst, 'v');
		goto name_finished;
	}

	obstack_1grow(&obst, 'J');

	/* mangle return type */
	if (n_ress == 0) {
		obstack_1grow(&obst, 'v');
	} else {
		assert(n_ress == 1);
		mangle_type(get_method_res_type(type, 0), &obst);
	}

	/* mangle parameter types */
	int start    = get_entity_allocation(entity) == allocation_static ? 0 : 1;
	if (n_params-start == 0) {
		obstack_1grow(&obst, 'v');
	} else {
		for (int i = start; i < n_params; ++i) {
			ir_type *parameter = get_method_param_type(type, i);
			mangle_type(parameter, &obst);
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

	flush_ct();

	obstack_grow(&obst, "_ZTV", 4);

	int emitted_N = mangle_qualified_class_name(clazz, 0, &obst);
	assert (emitted_N);

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

	memset(mangle_buffer, 0, 1024);

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
	flush_ct();
	obstack_free(&obst, NULL);
}
