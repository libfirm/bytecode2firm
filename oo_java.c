#include "oo_java.h"

#include "types.h"
#include "gcj_interface.h"
#include <libfirm/firm.h>
#include <liboo/oo.h>

#include <assert.h>
#include <string.h>

static void java_setup_mangling(void)
{
	// GCJ specific exceptional cases in name mangling
	mangle_set_primitive_type_name(type_byte, "c");
	mangle_set_primitive_type_name(type_char, "w");
	mangle_set_primitive_type_name(type_short, "s");
	mangle_set_primitive_type_name(type_int, "i");
	mangle_set_primitive_type_name(type_long, "x");
	mangle_set_primitive_type_name(type_boolean, "b");
	mangle_set_primitive_type_name(type_float, "f");
	mangle_set_primitive_type_name(type_double, "d");
	mangle_add_name_substitution("<init>", "C1");
	mangle_add_name_substitution("<clinit>", "18__U3c_clinit__U3e_");
	mangle_add_name_substitution("and", "3and$");
	mangle_add_name_substitution("or", "2or$");
	mangle_add_name_substitution("not", "3not$");
	mangle_add_name_substitution("xor", "3xor$");
	mangle_add_name_substitution("delete", "6delete$");
}

static int java_vtable_create_pred(ir_type *klass)
{
	class_t *linked_class = (class_t*) get_type_link(klass);
	if ((linked_class == NULL)
	 || (linked_class->access_flags & ACCESS_FLAG_INTERFACE) != 0) {
		// don't create a vtable, however, the interface's class$ must be constructed.
		if (! gcji_is_api_class(klass))
			gcji_construct_class_dollar_field(klass);

		return 0;
	}

	return 1;
}

static int java_vtable_include_pred(ir_entity *member)
{
	method_t *linked_method = (method_t *)get_entity_link(member);
	if ((linked_method == NULL)
	 || (linked_method->access_flags & ACCESS_FLAG_STATIC)
	 || (linked_method->access_flags & ACCESS_FLAG_PRIAVTE)
	 || (linked_method->access_flags & ACCESS_FLAG_FINAL) // calls to final methods are "devirtualized" when lowering the call.
	 || (strncmp(get_entity_name(member), "<init>", 6) == 0))
		return 0;

	return 1;
}

static int java_vtable_is_abstract_pred(ir_entity *member)
{
	method_t *linked_method = (method_t *)get_entity_link(member);
	assert (linked_method != NULL);
	return linked_method->access_flags & ACCESS_FLAG_ABSTRACT;
}

/*
 * vtable layout (a la gcj)
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

static void java_vtable_init_slots(ir_type *klass, ir_initializer_t *vtable_init, unsigned vtable_size)
{
	assert (vtable_size > 3); // setting initializers for slots 0..3 here.

	ir_graph *const_code = get_const_code_irg();
	ir_node *const_0 = new_r_Const_long(const_code, mode_reference, 0);
	for (int i = 0; i < 2; i++) {

		ir_initializer_t *val = create_initializer_const(const_0);
		set_initializer_compound_value (vtable_init, i, val);
	}

	ir_entity *class_dollar_field = gcji_get_class_dollar_field(klass);
	assert (class_dollar_field);

	if (! gcji_is_api_class(klass)) {
		class_dollar_field = gcji_construct_class_dollar_field(klass);
	}

	symconst_symbol cdf_sym;
	cdf_sym.entity_p = class_dollar_field;
	ir_node *cdf_symc = new_r_SymConst(const_code, mode_reference, cdf_sym, symconst_addr_ent);

	// FIXME: ^^ triggers a bug in the name mangling

	const char *cname = get_class_name(klass);
	int workaround = 0;
	for (; *cname != '\0' && !workaround; cname++)
		if (*cname == '$') workaround = 1;

	ir_initializer_t *cdf_init = create_initializer_const(workaround ? const_0 : cdf_symc);
	set_initializer_compound_value(vtable_init, 2, cdf_init);

	ir_initializer_t *gc_stuff_init = create_initializer_const(const_0);
	set_initializer_compound_value(vtable_init, 3, gc_stuff_init);
}

static ddispatch_binding java_call_decide_binding(ir_node* call)
{
	ir_node *callee = get_Call_ptr(call);
	if (is_SymConst(callee)
			&& get_SymConst_entity(callee) == builtin_arraylength) {
		return bind_builtin;
	}

	if (! is_Sel(callee))
		return bind_already_bound;

	ir_entity *method_entity = get_Sel_entity(callee);
	if (! is_method_entity(method_entity))
		return bind_already_bound;

	ir_type *classtype    = get_entity_owner(method_entity);
	if (! is_Class_type(classtype))
		return bind_already_bound;

	uint16_t cl_access_flags = ((class_t*)get_type_link(classtype))->access_flags;
	uint16_t mt_access_flags = ((method_t*)get_entity_link(method_entity))->access_flags;

	if ((cl_access_flags & ACCESS_FLAG_INTERFACE) != 0)
		return bind_interface;

	if ((cl_access_flags & ACCESS_FLAG_FINAL) + (mt_access_flags & ACCESS_FLAG_FINAL) != 0)
		return bind_static;

	return bind_dynamic;
}

static void java_call_lower_builtin(ir_node *call)
{
	ir_node *callee = get_Call_ptr(call);
	if (is_SymConst(callee)	&& get_SymConst_entity(callee) == builtin_arraylength) {
		dmemory_lower_arraylength(call);
		return;
	}
	assert(0);
}

extern ir_entity *vptr_entity;

void setup_liboo_for_java(void)
{
	ddispatch_params ddp;
	ddp.vtable_create_pred           = java_vtable_create_pred;
	ddp.vtable_include_pred          = java_vtable_include_pred;
	ddp.vtable_is_abstract_pred      = java_vtable_is_abstract_pred;
	ddp.vtable_init_slots            = java_vtable_init_slots;
	ddp.vtable_abstract_method_ident = new_id_from_str("_Jv_ThrowAbstractMethodError");
	ddp.vtable_vptr_points_to_index  = GCJI_VTABLE_OFFSET;
	ddp.vtable_index_of_first_method = GCJI_VTABLE_OFFSET + 2; // (class$, GC stuff)
	ddp.call_decide_binding          = java_call_decide_binding;
	ddp.call_lookup_interface_method = gcji_lookup_interface;
	ddp.call_lower_builtin           = java_call_lower_builtin;
	ddp.call_vptr_entity             = &vptr_entity;

	dmemory_params dmp;
	dmp.heap_alloc_object            = gcji_allocate_object;
	dmp.heap_alloc_array             = gcji_allocate_array;
	dmp.arraylength_get              = gcji_get_arraylength;

	oo_init(ddp, dmp);
	java_setup_mangling();
}
