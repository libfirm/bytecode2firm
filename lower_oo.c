#include "lower_oo.h"

#include <assert.h>
#include <libfirm/firm.h>

#include "adt/obst.h"
#include "adt/error.h"
#include "types.h"

static struct obstack obst;
static ir_entity *calloc_entity;

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

/**
 * special name mangler for "native" functions to make them more similar
 * to JNI names
 */
static ident *mangle_native_func(ir_entity *entity)
{
	assert(obstack_object_size(&obst) == 0);
	obstack_grow(&obst, "Java_", 5);

	ir_type *owner = get_entity_owner(entity);
	/* mangle class name */
	mangle_java_ident(get_class_ident(owner));
	obstack_1grow(&obst, '_');
	/* mangle entity name */
	ident      *entity_ident = get_entity_ident(entity);
	size_t      len          = get_id_strlen(entity_ident);
	const char *string       = get_id_str(entity_ident);
	obstack_grow(&obst, string, len);

	ir_type *type = get_entity_type(entity);
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

static ident *mangle_entity_name(ir_entity *entity)
{
	assert(obstack_object_size(&obst) == 0);


	/* mangle in a C++ like fashion so we can use c++filt to demangle */
	obstack_grow(&obst, "_ZN", 3);
	/* mangle class name */
	ir_type *owner       = get_entity_owner(entity);
	ident   *class_ident = get_class_ident(owner);
	const char *string = get_id_str(class_ident);
	size_t      len    = get_id_strlen(class_ident);
	const char *p      = string;
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
	/* mangle entity name */
	ident *entity_ident = get_entity_ident(entity);
	string              = get_id_str(entity_ident);
	len                 = get_id_strlen(entity_ident);
	if (strcmp(string, "<init>") == 0) {
		obstack_grow(&obst, "C1Ev", 2);
		goto name_finished;
	} else {
		obstack_printf(&obst, "%d%s", (int) len, string);
	}
	obstack_1grow(&obst, 'E');

	ir_type *type = get_entity_type(entity);
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

static void move_to_global(ir_entity *entity)
{
	ident *mangled;
	if (get_entity_visibility(entity) == ir_visibility_external) {
		mangled = mangle_native_func(entity);
	} else {
		mangled = mangle_entity_name(entity);
	}
	set_entity_ident(entity, mangled);
	set_entity_ld_ident(entity, mangled);

	/* move to global type */
	ir_type *owner = get_entity_owner(entity);
	assert(is_Class_type(owner));
	remove_class_member(owner, entity);
	set_entity_owner(entity, get_glob_type());
}

static void lower_type(type_or_ent tore, void *env)
{
	(void) env;
	if (get_kind(tore.typ) != k_type)
		return;

	ir_type *type = tore.typ;
	if (!is_Class_type(type)) {
		set_type_state(type, layout_fixed);
		return;
	}

	int n_members = get_class_n_members(type);
	for (int m = n_members-1; m >= 0; --m) {
		ir_entity *entity = get_class_member(type, m);
		if (is_method_entity(entity) ||
				get_entity_allocation(entity) == allocation_static) {
			move_to_global(entity);
		}
	}

	/* layout fields */
	default_layout_compound_type(type);
}

static void lower_node(ir_node *node, void *env)
{
	(void) env;
	unsigned addr_delta = 0;

	if (!is_Alloc(node))
		return;
	if (get_Alloc_where(node) != heap_alloc)
		return;

	ir_graph *irg   = get_irn_irg(node);
	ir_type  *type  = get_Alloc_type(node);
	ir_node  *count = get_Alloc_count(node);
	ir_node  *size;
	if (is_Array_type(type)) {
		ir_type *element_type = get_array_element_type(type);
		ir_node *block = get_nodes_block(node);
		count          = new_r_Conv(block, count, mode_Iu);
		unsigned count_size   = get_mode_size_bytes(mode_int);
		unsigned element_size = get_type_size_bytes(element_type);
		/* increase element count so we have enough space for a counter
		   at the front */
		unsigned add_size = (element_size + (count_size-1)) / count_size;
		ir_node *addv     = new_r_Const_long(irg, mode_Iu, add_size);
		ir_node *add1     = new_r_Add(block, count, addv, mode_Iu);
		ir_node *elsizev  = new_r_Const_long(irg, mode_Iu, element_size);

		size = new_r_Mul(block, add1, elsizev, mode_Iu);
		addr_delta = add_size * element_size;
	} else {
		assert(is_Const(count) && is_Const_one(count));
		symconst_symbol value;
		value.type_p = type;
		size = new_r_SymConst(irg, mode_Iu, value, symconst_type_size);
	}
	ir_node  *mem    = get_Alloc_mem(node);
	ir_node  *block  = get_nodes_block(node);
	symconst_symbol value;
	value.entity_p   = calloc_entity;
	ir_node  *callee = new_r_SymConst(irg, mode_reference, value, symconst_addr_ent);
	ir_node  *one    = new_r_Const_long(irg, mode_Iu, 1);
	ir_node  *in[2]  = { one, size };
	ir_type  *call_type = get_entity_type(calloc_entity);
	ir_node  *call   = new_r_Call(block, mem, callee, 2, in, call_type);

	ir_node  *new_mem = new_r_Proj(call, mode_M, pn_Call_M);
	ir_node  *ress    = new_r_Proj(call, mode_T, pn_Call_T_result);
	ir_node  *res     = new_r_Proj(ress, mode_reference, 0);

	if (addr_delta > 0) {
		ir_node *delta = new_r_Const_long(irg, mode_reference, (int)addr_delta);
		res = new_r_Add(block, res, delta, mode_reference);
	}

	turn_into_tuple(node, pn_Alloc_max);
	set_irn_n(node, pn_Alloc_M, new_mem);
	set_irn_n(node, pn_Alloc_X_regular, new_Bad());
	set_irn_n(node, pn_Alloc_X_except, new_Bad());
	set_irn_n(node, pn_Alloc_res, res);
}

static void lower_graph(ir_graph *irg)
{
	irg_walk_graph(irg, NULL, lower_node, NULL);
}

/**
 * Lower object oriented constructs
 */
void lower_oo(void)
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

	type_walk_prog(lower_type, NULL, NULL);


	ir_type *method_type = new_type_method(2, 1);
	ir_type *t_size_t    = new_type_primitive(mode_Iu);
	ir_type *t_ptr       = new_type_primitive(mode_reference);
	set_method_param_type(method_type, 0, t_size_t);
	set_method_param_type(method_type, 1, t_size_t);
	set_method_res_type(method_type, 0, t_ptr);

	ir_type *glob = get_glob_type();
	ident   *id   = new_id_from_str("calloc");
	calloc_entity = new_entity(glob, id, method_type);
	set_entity_visibility(calloc_entity, ir_visibility_external);

	int n_irgs = get_irp_n_irgs();
	for (int i = 0; i < n_irgs; ++i) {
		ir_graph *irg = get_irp_irg(i);
		lower_graph(irg);
	}

	lower_highlevel(0);

	obstack_free(&obst, NULL);
}
