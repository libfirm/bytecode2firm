#include "oo_java.h"

#include "types.h"
#include "gcj_interface.h"
#include "adt/obst.h"
#include <libfirm/firm.h>

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

static void java_construct_runtime_classinfo(ir_type *klass)
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
	ddispatch_set_runtime_classinfo_constructor(java_construct_runtime_classinfo);

	dmemory_set_allocation_methods(gcji_allocate_object, gcji_allocate_array, gcji_get_arraylength);
}

void oo_java_deinit(void)
{
	oo_deinit();
	obstack_free(&oo_info_obst, NULL);
}

bc2firm_type_info *create_class_info(class_t* javaclass)
{
	bc2firm_type_info *ci = obstack_alloc(&oo_info_obst, sizeof(bc2firm_type_info));
	ci->base.vptr = &vptr_entity;
	ci->base.needs_vtable = (javaclass->access_flags & ACCESS_FLAG_INTERFACE) == 0;
	ci->class_info = javaclass;
	return ci;
}

bc2firm_entity_info *create_method_info(method_t* javamethod, class_t* owner)
{
	bc2firm_entity_info *mi = obstack_alloc(&oo_info_obst, sizeof(bc2firm_entity_info));

	char *name = ((constant_utf8_string_t*)owner->constants[javamethod->name_index])->bytes;

	mi->base.include_in_vtable =
	 ! ((javamethod->access_flags & ACCESS_FLAG_STATIC)
	 || (javamethod->access_flags & ACCESS_FLAG_PRIAVTE)
	 || (javamethod->access_flags & ACCESS_FLAG_FINAL) // calls to final methods are "devirtualized" when lowering the call.
	 || (strncmp(name, "<init>", 6) == 0));

	mi->base.is_abstract = javamethod->access_flags & ACCESS_FLAG_ABSTRACT;

	if (! mi->base.include_in_vtable || (owner->access_flags & ACCESS_FLAG_FINAL))
		mi->base.binding = bind_static;
	else if ((owner->access_flags & ACCESS_FLAG_INTERFACE))
		mi->base.binding = bind_interface;
	else
		mi->base.binding = bind_dynamic;

	mi->member_info.method_info = javamethod;

	return mi;
}

bc2firm_entity_info *create_field_info(field_t* javafield, class_t* owner)
{
	(void) owner;
	bc2firm_entity_info *fi = obstack_alloc(&oo_info_obst, sizeof(bc2firm_entity_info));
	fi->base.include_in_vtable = 0;
	fi->base.binding = bind_static;
	fi->member_info.field_info = javafield;
	return fi;
}
