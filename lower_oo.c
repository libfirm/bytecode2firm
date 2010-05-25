#include "lower_oo.h"

#include <assert.h>
#include <libfirm/firm.h>

#include "adt/error.h"
#include "types.h"

#include "class_file.h"
#include "mangle.h"

static ir_type *type_reference;
static unsigned type_reference_size;
static ir_type *global_type;

static void init_types(void)
{
	type_reference = new_type_primitive(mode_reference);
	type_reference_size = get_type_size_bytes(type_reference);
	global_type = get_glob_type();
}

static ir_entity *calloc_entity;

static void move_to_global(ir_entity *entity)
{
	/* move to global type */
	ir_type *owner = get_entity_owner(entity);
	assert(is_Class_type(owner));
	remove_class_member(owner, entity);
	set_entity_owner(entity, global_type);
}

static void setup_vtable(ir_type *clazz, void *env)
{
	(void) env;
	assert(is_Class_type(clazz));

	ir_type *superclass = NULL;
	int vtable_size = 0;
	if (get_class_n_supertypes(clazz) > 0) {
		 superclass = get_class_supertype(clazz, 0);
		 vtable_size = get_class_vtable_size(superclass);
	}
	set_class_vtable_size(clazz, vtable_size);

	// assign vtable ids
	for (int i = 0; i < get_class_n_members(clazz); i++) {
		ir_entity *member = get_class_member(clazz, i);
		if (is_method_entity(member)
			&& ! (((method_t *)get_entity_link(member))->access_flags & ACCESS_FLAG_STATIC)) {
			if (get_entity_n_overwrites(member) > 0) { // this method already has a vtable id, copy it from the superclass' implementation
				ir_entity *overwritten_entity = get_entity_overwrites(member, 0);
				set_entity_vtable_number(member, get_entity_vtable_number(overwritten_entity));
			} else {
				set_entity_vtable_number(member, vtable_size);
				set_class_vtable_size(clazz, ++vtable_size);
			}
		}
	}

	// the vtable currently is an array of pointers
	ir_type *vtable_type = new_type_array(1, type_reference);
	set_array_bounds_int(vtable_type, 0, 0, vtable_size);
	set_type_size_bytes(vtable_type, type_reference_size * vtable_size);
	set_type_state(vtable_type, layout_fixed);

	ir_entity *vtable = new_entity(global_type, mangle_vtable_name(clazz), vtable_type);

	ir_graph *const_code = get_const_code_irg();
	ir_initializer_t * init = create_initializer_compound(vtable_size);

	if (superclass != NULL) {
		int superclass_vtable_size = get_class_vtable_size(superclass);
		ir_entity *superclass_vtable_entity = get_class_member_by_name(global_type, mangle_vtable_name(superclass));
		assert (superclass_vtable_entity != NULL);
		ir_initializer_t *superclass_vtable_init = get_entity_initializer(superclass_vtable_entity);

		// copy vtable initialization from superclass
		for (int i = 0; i < superclass_vtable_size; i++) {
				ir_initializer_t *superclass_vtable_init_value = get_initializer_compound_value(superclass_vtable_init, i);
				set_initializer_compound_value (init, i, superclass_vtable_init_value);
		}
	}

	// setup / replace vtable entries to point to clazz's implementation
	for (int i = 0; i < get_class_n_members(clazz); i++) {
		ir_entity *member = get_class_member(clazz, i);
		if (is_method_entity(member)) {
			int member_vtid = get_entity_vtable_number(member);
			if (member_vtid >= 0) {
				union symconst_symbol sym;
				sym.entity_p = member;
				ir_node *symconst_node = new_r_SymConst(const_code, mode_P, sym, symconst_addr_ent);
				ir_initializer_t *val = create_initializer_const(symconst_node);
				set_initializer_compound_value (init, member_vtid, val);
			}
		}
	}

	set_entity_initializer(vtable, init);

//	dump_type(clazz);
//	dump_entity(vtable);
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

	if (type == global_type)
		return;

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
}

void prepare_oo(void) {
	init_types();

	class_walk_super2sub(setup_vtable, NULL, NULL);
}
