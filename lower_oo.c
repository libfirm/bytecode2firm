#include "lower_oo.h"

#include <assert.h>
#include <libfirm/firm.h>

static void lower_class_member(ir_entity *entity)
{
	ir_type *type = get_entity_type(entity);
	if (!is_Method_type(type))
		return;

	/* mangle method name */
	ir_type *owner        = get_entity_owner(entity);
	ident   *class_ident  = get_class_ident(owner);
	ident   *method_ident = get_entity_ident(entity);
	ident   *mangled      = id_mangle_dot(class_ident, method_ident);
	set_entity_ident(entity, mangled);
	set_entity_ld_ident(entity, mangled);

	assert(is_Class_type(owner));

	/* move to global type */
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
		return;
	}

	int n_members = get_class_n_members(type);
	for (int m = n_members-1; m >= 0; --m) {
		ir_entity *entity = get_class_member(type, m);
		lower_class_member(entity);
	}
}

/**
 * Lower object oriented constructs
 */
void lower_oo(void)
{
	type_walk_prog(lower_type, NULL, NULL);
}
