#include "oo_java.h"

#include "types.h"
#include "gcj_interface.h"
#include "adt/obst.h"

#include <liboo/ddispatch.h>
#include <liboo/dmemory.h>

#include <liboo/rtti.h>
#include <liboo/nodes.h>

#include <assert.h>
#include <string.h>

/*
 * vtable layout (a la gcj)
 *
 * _ZTVNxyzE:
 *   <vtable slot 0> _ZNxyz6class$E
 *   <vtable slot 1> GC bitmap marking descriptor
 *   <vtable slot 2> addr(first method)
 *   ...
 *   <vtable slot n> addr(last method)
 */

static void java_init_vtable_slots(ir_type *klass, ir_initializer_t *vtable_init, unsigned vtable_size)
{
	ir_graph *const_code = get_const_code_irg();

	ir_entity *rtti      = oo_get_class_rtti_entity(klass);
	ir_node   *rtti_addr = new_r_Address(const_code, rtti);

	ir_initializer_t *rtti_init = create_initializer_const(rtti_addr);

	assert(vtable_size > 0); // setting initializers for slots 0..3 here.
	set_initializer_compound_value(vtable_init, 0, rtti_init);
}

static void dummy(ir_type *t)
{
	(void)t;
}

void oo_java_init(void)
{
	oo_init();

	ddispatch_set_vtable_layout(0, 1, 0, java_init_vtable_slots);
	ddispatch_set_abstract_method_ident(new_id_from_str("_Jv_ThrowAbstractMethodError"));
	ddispatch_set_interface_lookup_constructor(gcji_lookup_interface);

	rtti_set_runtime_typeinfo_constructor(dummy);
	rtti_set_instanceof_constructor(gcji_instanceof);

	dmemory_set_allocation_methods(gcji_get_arraylength);
}

void oo_java_deinit(void)
{
	oo_deinit();
}
