#include "oo_java.h"

#include "types.h"
#include "gcj_interface.h"
#include "mangle.h"
#include "adt/obst.h"

#include <liboo/ddispatch.h>
#include <liboo/dmemory.h>

#include <liboo/rtti.h>
#include <liboo/oo_nodes.h>

#include <assert.h>
#include <string.h>

extern ir_entity  *vptr_entity;
static const char *class_info_name = "CI$";

/*
 * vtable layout (a la gcj, hacked to use the liboo classinfo in parallel)
 *
 * _ZTVNxyzE:
 *   0                                  \  GCJI_VTABLE_OFFSET
 *                   _ZNxyz3CI$         /                       liboo class info
 *   <vtable slot 0> _ZNxyz6class$E     <-- vptrs point here    gcj class info
 *   <vtable slot 1> GC bitmap marking descriptor
 *   <vtable slot 2> addr(first method)
 *   ...
 *   <vtable slot n> addr(last method)
 */

static void java_init_vtable_slots(ir_type *klass, ir_initializer_t *vtable_init, unsigned vtable_size)
{
	assert (vtable_size > 3); // setting initializers for slots 0..3 here.

	ir_graph *const_code = get_const_code_irg();
	ir_node *const_0 = new_r_Const_long(const_code, mode_reference, 0);
	ir_initializer_t *init_0 = create_initializer_const(const_0);
	set_initializer_compound_value (vtable_init, 0, init_0);

	ir_entity *oo_ci = oo_get_class_rtti_entity(klass);
	assert (oo_ci);
	symconst_symbol oo_ci_sym;
	oo_ci_sym.entity_p = oo_ci;
	ir_node *oo_ci_symc = new_r_SymConst(const_code, mode_reference, oo_ci_sym, symconst_addr_ent);
	ir_initializer_t *oo_ci_init = create_initializer_const(oo_ci_symc);
	set_initializer_compound_value(vtable_init, 1, oo_ci_init);

	ir_entity *class_dollar_field = gcji_get_class_dollar_field(klass);
	assert (class_dollar_field);
	symconst_symbol cdf_sym;
	cdf_sym.entity_p = class_dollar_field;
	ir_node *cdf_symc = new_r_SymConst(const_code, mode_reference, cdf_sym, symconst_addr_ent);

	const char *cname = get_class_name(klass);
	int workaround = 0;
	for (; *cname != '\0' && !workaround; cname++)
		if (*cname == '$') workaround = 1;

	ir_initializer_t *cdf_init = create_initializer_const(workaround ? const_0 : cdf_symc);
	set_initializer_compound_value(vtable_init, 2, cdf_init);

	set_initializer_compound_value(vtable_init, 3, init_0);
}

static void java_construct_runtime_typeinfo(ir_type *klass)
{
	rtti_default_construct_runtime_typeinfo(klass);
	if (! gcji_is_api_class(klass))
		gcji_construct_class_dollar_field(klass);
}

void oo_java_init(void)
{
	mangle_init();
	oo_init();

	ddispatch_set_vtable_layout(GCJI_VTABLE_OFFSET, GCJI_VTABLE_OFFSET+2, GCJI_VTABLE_OFFSET-1, java_init_vtable_slots);
	ddispatch_set_abstract_method_ident(new_id_from_str("_Jv_ThrowAbstractMethodError"));

	dmemory_set_allocation_methods(gcji_allocate_object, gcji_allocate_array, gcji_get_arraylength);

	rtti_set_runtime_typeinfo_constructor(java_construct_runtime_typeinfo);
}

void oo_java_deinit(void)
{
	oo_deinit();
	mangle_deinit();
}

void oo_java_setup_type_info(ir_type *classtype, class_t* javaclass)
{
	oo_set_class_is_final(classtype,     javaclass->access_flags & ACCESS_FLAG_FINAL);
	oo_set_class_is_abstract(classtype,  javaclass->access_flags & ACCESS_FLAG_ABSTRACT);
	oo_set_class_is_interface(classtype, javaclass->access_flags & ACCESS_FLAG_INTERFACE);

	if (! oo_get_class_is_interface(classtype)) {
		const char *classname  = get_class_name(classtype);
		ident *vtable_ident    = id_mangle_dot(get_class_ident(classtype), new_id_from_str("vtable"));
		ident *vtable_ld_ident = mangle_vtable_name(classname);

		ir_entity *vtable = new_entity(get_glob_type(), vtable_ident, type_reference);
		set_entity_ld_ident(vtable, vtable_ld_ident);
		oo_set_class_vtable_entity(classtype, vtable);
	}

	oo_set_class_vptr_entity(classtype, vptr_entity);
	oo_set_type_link(classtype, javaclass);
	javaclass->link = classtype;

	const char *classname  = get_class_name(classtype);
	ident *mangled_id = mangle_member_name(classname, class_info_name, NULL);
	ir_entity *ci = new_entity(get_glob_type(), mangled_id, type_reference);
	oo_set_class_rtti_entity(classtype, ci);
}

void oo_java_setup_method_info(ir_entity* method, method_t* javamethod, class_t *defining_javaclass)
{
	const char *name               = ((constant_utf8_string_t*)defining_javaclass->constants[javamethod->name_index])->bytes;
	uint16_t    accs               = javamethod->access_flags;
	uint16_t    owner_access_flags = defining_javaclass->access_flags;

	oo_set_method_is_abstract(method, accs & ACCESS_FLAG_ABSTRACT);
	oo_set_method_is_final(method,    (accs | owner_access_flags) & ACCESS_FLAG_FINAL);

	int is_constructor = strncmp(name, "<init>", 6) == 0;
	int exclude_from_vtable =
	   ((accs & ACCESS_FLAG_STATIC)
	 || (accs & ACCESS_FLAG_PRIVATE)
	 || (accs & ACCESS_FLAG_FINAL) // calls to final methods are "devirtualized" when lowering the call.
	 || (is_constructor));

	oo_set_method_exclude_from_vtable(method, exclude_from_vtable);

	ddispatch_binding binding = bind_unknown;
	if (exclude_from_vtable || (owner_access_flags & ACCESS_FLAG_FINAL))
		binding = bind_static;
	else if ((owner_access_flags & ACCESS_FLAG_INTERFACE))
		binding = bind_interface;
	else
		binding = bind_dynamic;

	oo_set_entity_binding(method, binding);

	oo_set_entity_link(method, javamethod);
	javamethod->link = method;
}

void oo_java_setup_field_info(ir_entity *field, field_t* javafield)
{
	oo_set_entity_link(field, javafield);
	javafield->link = field;
}
