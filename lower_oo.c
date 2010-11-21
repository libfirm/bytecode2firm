#include "lower_oo.h"

#include <assert.h>
#include <string.h>
#include <libfirm/firm.h>
#include <liboo/mangle.h>

#include "adt/error.h"
#include "types.h"

#include "class_file.h"
#include "gcj_interface.h"

static ir_entity *calloc_entity;
extern ir_entity *vptr_entity; // there's exactly one vptr entity, member of java.lang.Object.

static ident *abstract_method_ident;

/*
 * vtable layout (a la gcj / c++)
 *
 * _ZTVNxyzE:
 *   0                                  \  GCJI_VTABLE_OFFSET
 *   0                                  /
 *   <vtable slot 0> _ZNxyz6class$E     <-- vptrs point here
 *   <vtable slot 1> GC bitmap marking descriptor
 *   <vtable slot 2> addr(first method)
 *   ...
 *   <vtable slot n> addr(last method)
 */


static void move_to_global(ir_entity *entity)
{
	/* move to global type */
	ir_type *owner = get_entity_owner(entity);
	assert(is_Class_type(owner));
	ir_type *global_type = get_glob_type();
	set_entity_owner(entity, global_type);
}

static void setup_vtable(ir_type *clazz, void *env)
{
	(void) env;
	assert(is_Class_type(clazz));

	class_t *linked_class = (class_t*) get_type_link(clazz);
	if (linked_class == NULL) return;

	if ((linked_class->access_flags & ACCESS_FLAG_INTERFACE) != 0) {
		if (! gcji_is_api_class(clazz)) {
			gcji_construct_class_dollar_field(clazz);
		}
		return;
	}

	ident *vtable_name = mangle_vtable_name(clazz);

	ir_type *global_type = get_glob_type();
	assert (get_class_member_by_name(global_type, vtable_name) == NULL);

	ir_type *superclass = NULL;
	unsigned vtable_size = 2; // see above, (class$, GC stuff)
	int n_supertypes = get_class_n_supertypes(clazz);
	if (n_supertypes > 0) {
		assert (n_supertypes == 1);
		superclass = get_class_supertype(clazz, 0);
		vtable_size = get_class_vtable_size(superclass);
	}
	set_class_vtable_size(clazz, vtable_size);

	// assign vtable ids
	for (int i = 0; i < get_class_n_members(clazz); i++) {
		ir_entity *member = get_class_member(clazz, i);
		if (is_method_entity(member)) {
			method_t *linked_method = (method_t *)get_entity_link(member);

			if (! (linked_method->access_flags & ACCESS_FLAG_STATIC)
			 && ! (linked_method->access_flags & ACCESS_FLAG_PRIAVTE)
			 && ! (linked_method->access_flags & ACCESS_FLAG_FINAL) // calls to final methods are "devirtualized" when lowering the call.
			 && ! (strncmp(get_entity_name(member), "<init>", 6) == 0)) {
				int n_overwrites = get_entity_n_overwrites(member);
				if (n_overwrites > 0) { // this method already has a vtable id, copy it from the superclass' implementation
					assert (n_overwrites == 1);
					ir_entity *overwritten_entity = get_entity_overwrites(member, 0);
					unsigned vtable_id = get_entity_vtable_number(overwritten_entity);
					assert (vtable_id != IR_VTABLE_NUM_NOT_SET);
					set_entity_vtable_number(member, vtable_id);
				} else {
					set_entity_vtable_number(member, vtable_size);
					set_class_vtable_size(clazz, ++vtable_size);
				}
			}
		}
	}

	// the vtable currently is an array of pointers
	unsigned type_reference_size = get_type_size_bytes(type_reference);
	ir_type *vtable_type = new_type_array(1, type_reference);
	size_t vtable_ent_size = vtable_size + GCJI_VTABLE_OFFSET;
	set_array_bounds_int(vtable_type, 0, 0, vtable_ent_size);
	set_type_size_bytes(vtable_type, type_reference_size * vtable_ent_size);
	set_type_state(vtable_type, layout_fixed);

	ir_entity *vtable = new_entity(global_type, vtable_name, vtable_type);
	set_entity_alignment(vtable, 32);

	ir_graph *const_code = get_const_code_irg();
	ir_initializer_t * init = create_initializer_compound(vtable_ent_size);

	if (superclass != NULL) {
		unsigned superclass_vtable_size = get_class_vtable_size(superclass);
		ir_entity *superclass_vtable_entity = get_class_member_by_name(global_type, mangle_vtable_name(superclass));
		assert (superclass_vtable_entity != NULL);
		ir_initializer_t *superclass_vtable_init = get_entity_initializer(superclass_vtable_entity);

		// copy vtable initialization from superclass
		for (unsigned i = GCJI_VTABLE_OFFSET; i < superclass_vtable_size+GCJI_VTABLE_OFFSET; i++) {
				ir_initializer_t *superclass_vtable_init_value = get_initializer_compound_value(superclass_vtable_init, i);
				set_initializer_compound_value (init, i, superclass_vtable_init_value);
		}
	}

	// setup / replace vtable entries to point to clazz's implementation
	for (int i = 0; i < get_class_n_members(clazz); i++) {
		ir_entity *member = get_class_member(clazz, i);
		if (is_method_entity(member)) {
			unsigned member_vtid = get_entity_vtable_number(member);
			if (member_vtid != IR_VTABLE_NUM_NOT_SET) {
				method_t *linked_method = (method_t*) get_entity_link(member);
				assert (linked_method != NULL);

				union symconst_symbol sym;
				if ((linked_method->access_flags & ACCESS_FLAG_ABSTRACT) == 0) {
					sym.entity_p = member;
				} else {
					sym.entity_p = new_entity(get_glob_type(), abstract_method_ident, get_entity_type(member));
				}
				ir_node *symconst_node = new_r_SymConst(const_code, mode_P, sym, symconst_addr_ent);
				ir_initializer_t *val = create_initializer_const(symconst_node);
				set_initializer_compound_value (init, member_vtid+GCJI_VTABLE_OFFSET, val);
			}
		}
	}

	ir_node *const_0 = new_r_Const_long(const_code, mode_reference, 0);
	for (int i = 0; i < 2; i++) {

		ir_initializer_t *val = create_initializer_const(const_0);
		set_initializer_compound_value (init, i, val);
	}

	ir_entity *class_dollar_field = gcji_get_class_dollar_field(clazz);
	assert (class_dollar_field);

	if (! gcji_is_api_class(clazz)) {
		class_dollar_field = gcji_construct_class_dollar_field(clazz);
	}

	symconst_symbol cdf_sym;
	cdf_sym.entity_p = class_dollar_field;
	ir_node *cdf_symc = new_r_SymConst(const_code, mode_reference, cdf_sym, symconst_addr_ent);

	// FIXME: ^^ triggers a bug in the name mangling

	const char *cname = get_class_name(clazz);
	int workaround = 0;
	for (; *cname != '\0' && !workaround; cname++)
		if (*cname == '$') workaround = 1;

	ir_initializer_t *cdf_init = create_initializer_const(workaround ? const_0 : cdf_symc);
	set_initializer_compound_value(init, 2, cdf_init);

	ir_initializer_t *gc_stuff_init = create_initializer_const(const_0);
	set_initializer_compound_value(init, 3, gc_stuff_init);

	set_entity_initializer(vtable, init);
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

	ir_type *global_type = get_glob_type();
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

static void lower_Alloc(ir_node *node)
{
	assert(is_Alloc(node));

	if (get_Alloc_where(node) != heap_alloc)
		return;

	ir_graph *irg   = get_irn_irg(node);
	ir_type  *type  = get_Alloc_type(node);
	ir_node  *count = get_Alloc_count(node);

	ir_node  *res   = NULL;

	ir_node  *cur_mem = get_Alloc_mem(node);
	ir_node  *block   = get_nodes_block(node);

	ir_type  *eltype   = is_Array_type(type) ? get_array_element_type(type) : NULL;

	if (is_Class_type(type)) {
		res = gcji_allocate_object(type, irg, block, &cur_mem);
	} else if (is_Array_type(type)) {
		res = gcji_allocate_array(eltype, count, irg, block, &cur_mem);
	} else {
		assert (0);
	}

	if (is_Class_type(type)) {
		ir_node   *vptr          = new_r_Sel(block, new_r_NoMem(irg), res, 0, NULL, vptr_entity);

		ir_type   *global_type   = get_glob_type();
		ir_entity *vtable_entity = get_class_member_by_name(global_type, mangle_vtable_name(type));

		union symconst_symbol sym;
		sym.entity_p = vtable_entity;
		ir_node   *vtable_symconst = new_r_SymConst(irg, mode_reference, sym, symconst_addr_ent);
		ir_node   *const_offset    = new_r_Const_long(irg, mode_reference, GCJI_VTABLE_OFFSET * get_type_size_bytes(type_reference));
		ir_node   *vptr_target     = new_r_Add(block, vtable_symconst, const_offset, mode_reference);
		ir_node   *vptr_store      = new_r_Store(block, cur_mem, vptr, vptr_target, cons_none);
		           cur_mem         = new_r_Proj(vptr_store, mode_M, pn_Store_M);
	}

	turn_into_tuple(node, pn_Alloc_max);
	set_irn_n(node, pn_Alloc_M, cur_mem);
	set_irn_n(node, pn_Alloc_res, res);
}

static void lower_arraylength(ir_node *call)
{
	ir_node  *array_ref = get_Call_param(call, 0);

	ir_node  *block     = get_nodes_block(call);
	ir_graph *irg       = get_irn_irg(block);

	ir_node  *cur_mem   = get_Call_mem(call);

	ir_node  *len       = gcji_get_arraylength(array_ref, irg, block, &cur_mem);

	ir_node  *in[]      = { len };
	ir_node  *lent      = new_r_Tuple(block, sizeof(in)/sizeof(*in), in);

	turn_into_tuple(call, pn_Call_max);
	set_irn_n(call, pn_Call_M, cur_mem);
	set_irn_n(call, pn_Call_T_result, lent);
}

static void lower_Call(ir_node* call)
{
	assert(is_Call(call));

	ir_node *callee = get_Call_ptr(call);
	if (is_SymConst(callee)
			&& get_SymConst_entity(callee) == builtin_arraylength) {
		lower_arraylength(call);
		return;
	}

	if (! is_Sel(callee))
		return;

	ir_node *objptr = get_Sel_ptr(callee);
	ir_entity *method_entity = get_Sel_entity(callee);
	if (! is_method_entity(method_entity))
		return;

	ir_type *classtype    = get_entity_owner(method_entity);
	if (! is_Class_type(classtype))
		return;

	uint16_t cl_access_flags = ((class_t*)get_type_link(classtype))->access_flags;
	uint16_t mt_access_flags = ((method_t*)get_entity_link(method_entity))->access_flags;

	ir_graph *irg         = get_irn_irg(call);
	ir_node  *block       = get_nodes_block(call);
	ir_node  *cur_mem     = get_Call_mem(call);
	ir_node  *real_callee = NULL;

	if ((cl_access_flags & ACCESS_FLAG_INTERFACE) != 0) {
		real_callee           = gcji_lookup_interface(objptr, classtype, method_entity, irg, block, &cur_mem);
	} else {
		int link_static       = (cl_access_flags & ACCESS_FLAG_FINAL) + (mt_access_flags & ACCESS_FLAG_FINAL) != 0;

		if (! link_static) {
			ir_node *vptr         = new_r_Sel(block, new_r_NoMem(irg), objptr, 0, NULL, vptr_entity);


			ir_node *vtable_load  = new_r_Load(block, cur_mem, vptr, mode_P, cons_none);
			ir_node *vtable_addr  = new_r_Proj(vtable_load, mode_P, pn_Load_res);
					 cur_mem      = new_r_Proj(vtable_load, mode_M, pn_Load_M);

			unsigned vtable_id    = get_entity_vtable_number(method_entity);
			assert(vtable_id != IR_VTABLE_NUM_NOT_SET);

			unsigned type_reference_size = get_type_size_bytes(type_reference);
			ir_node *vtable_offset= new_r_Const_long(irg, mode_P, vtable_id * type_reference_size);
			ir_node *funcptr_addr = new_r_Add(block, vtable_addr, vtable_offset, mode_P);
			ir_node *callee_load  = new_r_Load(block, cur_mem, funcptr_addr, mode_P, cons_none);
					 real_callee  = new_r_Proj(callee_load, mode_P, pn_Load_res);
					 cur_mem      = new_r_Proj(callee_load, mode_M, pn_Load_M);
		} else {
			symconst_symbol callee;
			callee.entity_p = method_entity;
			real_callee = new_r_SymConst(irg, mode_reference, callee, symconst_addr_ent);
		}
	}

	set_Call_ptr(call, real_callee);
	set_Call_mem(call, cur_mem);
}

static void lower_node(ir_node *node, void *env)
{
	(void) env;
	if (is_Alloc(node)) {
		lower_Alloc(node);
	} else if (is_Call(node)) {
		lower_Call(node);
	}
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
	abstract_method_ident = new_id_from_str("_Jv_ThrowAbstractMethodError");

	class_walk_super2sub(setup_vtable, NULL, NULL);

	ir_type *method_type = new_type_method(2, 1);
	ir_type *t_size_t    = new_type_primitive(mode_Iu);
	ir_type *t_ptr       = new_type_primitive(mode_reference);
	set_method_param_type(method_type, 0, t_size_t);
	set_method_param_type(method_type, 1, t_size_t);
	set_method_res_type(method_type, 0, t_ptr);
	set_method_additional_properties(method_type, mtp_property_malloc);

	ir_type *glob = get_glob_type();
	ident   *id   = new_id_from_str("calloc");
	calloc_entity = new_entity(glob, id, method_type);
	set_entity_visibility(calloc_entity, ir_visibility_external);

	int n_irgs = get_irp_n_irgs();
	for (int i = 0; i < n_irgs; ++i) {
		ir_graph *irg = get_irp_irg(i);
		lower_graph(irg);
	}

	type_walk_super2sub(lower_type, NULL, NULL);

	//dump_all_ir_graphs("before_highlevel");
	lower_highlevel(0);
}
