#include "oo_java.h"

#include "types.h"
#include "gcj_interface.h"
#include "adt/obst.h"

#include <liboo/ddispatch.h>
#include <liboo/dmemory.h>
#include <liboo/mangle.h>
#include <liboo/rtti.h>

#include <assert.h>
#include <string.h>

static struct obstack oo_info_obst;
extern ir_entity *vptr_entity;

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

static void java_init_vtable_slots(ir_type *klass, ir_initializer_t *vtable_init, unsigned vtable_size)
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

static void java_construct_runtime_typeinfo(ir_type *klass)
{
	assert (is_Class_type(klass));
	if (klass == get_glob_type())
		return;

	if (! gcji_is_api_class(klass)) {
		gcji_construct_class_dollar_field(klass);
	}
}

void oo_java_init(void)
{
	obstack_init(&oo_info_obst);
	oo_init();

	java_setup_mangling();

	ddispatch_set_vtable_layout(GCJI_VTABLE_OFFSET, GCJI_VTABLE_OFFSET+2, java_init_vtable_slots);
	ddispatch_set_interface_lookup_constructor(gcji_lookup_interface);
	ddispatch_set_abstract_method_ident(new_id_from_str("_Jv_ThrowAbstractMethodError"));

	dmemory_set_allocation_methods(gcji_allocate_object, gcji_allocate_array, gcji_get_arraylength);

	rtti_set_runtime_typeinfo_constructor(java_construct_runtime_typeinfo);
}

void oo_java_deinit(void)
{
	oo_deinit();
	obstack_free(&oo_info_obst, NULL);
}

void oo_java_setup_type_info(ir_type *classtype, class_t* javaclass)
{
	oo_set_class_omit_vtable(classtype, (javaclass->access_flags & ACCESS_FLAG_INTERFACE) != 0);
	oo_set_class_vptr_entity(classtype, vptr_entity);
	oo_set_type_link(classtype, javaclass);
	javaclass->link = classtype;
}

void oo_java_setup_method_info(ir_entity* method, method_t* javamethod, ir_type *defining_class, uint16_t owner_access_flags)
{
	const char *name = get_entity_name(method);
	uint16_t    accs = javamethod->access_flags;

	int is_constructor = strncmp(name, "<init>", 6) == 0;
	int exclude_from_vtable =
	   ((accs & ACCESS_FLAG_STATIC)
	 || (accs & ACCESS_FLAG_PRIVATE)
	 || (accs & ACCESS_FLAG_FINAL) // calls to final methods are "devirtualized" when lowering the call.
	 || (is_constructor));
	oo_set_method_exclude_from_vtable(method, exclude_from_vtable);
	oo_set_method_is_abstract(method, accs & ACCESS_FLAG_ABSTRACT);
	oo_set_method_is_constructor(method, is_constructor);

	ddispatch_binding binding = bind_unknown;
	if (exclude_from_vtable || (owner_access_flags & ACCESS_FLAG_FINAL))
		binding = bind_static;
	else if ((owner_access_flags & ACCESS_FLAG_INTERFACE))
		binding = bind_interface;
	else
		binding = bind_dynamic;

	oo_set_entity_binding(method, binding);

	if (accs & ACCESS_FLAG_STATIC)
		oo_set_entity_alt_namespace(method, defining_class);

	oo_set_entity_link(method, javamethod);
	javamethod->link = method;
}

void oo_java_setup_field_info(ir_entity *field, field_t* javafield, ir_type *defining_class)
{
	if (javafield->access_flags & ACCESS_FLAG_STATIC)
		oo_set_entity_alt_namespace(field, defining_class);
	oo_set_entity_link(field, javafield);
	javafield->link = field;
}
