
#include "gcj_interface.h"
#include "types.h"

#include <libfirm/firm.h>

#include <assert.h>
#include <string.h>

static ident     *class_dollar_ident;
static ir_type   *glob;
static ir_entity *gcj_alloc_entity;
static ir_entity *gcj_init_entity;
static ir_entity *gcj_new_string_entity;

static ir_entity *add_class_dollar_field_recursive(ir_type *type)
{
	assert (is_Class_type(type));
	assert (gcji_is_api_class(type));

	ir_entity *superclass_class_dollar_field = NULL;

	int n_supertypes = get_class_n_supertypes(type);
	if (n_supertypes > 0) {
		assert(n_supertypes == 1);
		ir_type *superclass = get_class_supertype(type, 0);
		superclass_class_dollar_field = get_class_member_by_name(superclass, class_dollar_ident);
		if (superclass_class_dollar_field == NULL) {
			superclass_class_dollar_field = add_class_dollar_field_recursive(superclass);
		}
	}

	ir_entity *class_dollar_field = new_entity(type, class_dollar_ident, type_reference);
	if (superclass_class_dollar_field != NULL) {
		add_entity_overwrites(class_dollar_field, superclass_class_dollar_field);
	}
	ident *mangled_id = mangle_entity_name(class_dollar_field, class_dollar_ident);
	set_entity_ld_ident(class_dollar_field, mangled_id);

	return class_dollar_field;
}

int gcji_is_api_class(ir_type *type)
{
	assert (is_Class_type(type));
	const char *classname = get_class_name(type);
	return strncmp("java/", classname, 5)  == 0
		|| strncmp("javax/", classname, 6) == 0
		|| strncmp("gnu/", classname, 4)   == 0;
}

void gcji_init()
{
	class_dollar_ident = new_id_from_str("class$");
	glob               = get_glob_type();

	ir_type *t_ptr     = new_type_primitive(mode_reference);

	// gcj_alloc
	ir_type *gcj_alloc_method_type = new_type_method(1, 1);
	set_method_param_type(gcj_alloc_method_type, 0, t_ptr);
	set_method_res_type(gcj_alloc_method_type, 0, t_ptr);
	set_method_additional_property(gcj_alloc_method_type, mtp_property_malloc);

	ident   *gcj_alloc_id = new_id_from_str("_Jv_AllocObjectNoInitNoFinalizer");
	gcj_alloc_entity = new_entity(glob, gcj_alloc_id, gcj_alloc_method_type);
	set_entity_visibility(gcj_alloc_entity, ir_visibility_external);

	// gcj_init
	ir_type *gcj_init_method_type = new_type_method(1, 0);
	set_method_param_type(gcj_init_method_type, 0, t_ptr);

	ident   *gcj_init_id = new_id_from_str("_Jv_InitClass");
	gcj_init_entity = new_entity(glob, gcj_init_id, gcj_init_method_type);
	set_entity_visibility(gcj_init_entity, ir_visibility_external);

	// gcj_new_string
	ir_type *gcj_new_string_method_type = new_type_method(1, 1);
	set_method_param_type(gcj_new_string_method_type, 0, t_ptr);
	set_method_res_type(gcj_new_string_method_type, 0, t_ptr);

	ident   *gcj_new_string_id = new_id_from_str("_Jv_NewStringUTF");
	gcj_new_string_entity = new_entity(glob, gcj_new_string_id, gcj_new_string_method_type);
	set_entity_visibility(gcj_new_string_entity, ir_visibility_external);
}

void gcji_class_init(ir_type *type, ir_graph *irg, ir_node *block, ir_node **mem)
{
	assert (is_Class_type(type));
	assert (gcji_is_api_class(type));

	symconst_symbol init_sym;
	init_sym.entity_p = gcj_init_entity;
	ir_node *init_callee = new_r_SymConst(irg, mode_reference, init_sym, symconst_addr_ent);

	ir_entity *class_dollar_field = get_class_member_by_name(type, class_dollar_ident);
	if (class_dollar_field == NULL) {
		class_dollar_field = add_class_dollar_field_recursive(type);
	}

	symconst_symbol class_dollar_sym;
	class_dollar_sym.entity_p   = class_dollar_field;
	ir_node *class_dollar_symc = new_r_SymConst(irg, mode_reference, class_dollar_sym, symconst_addr_ent);

	ir_node *init_args[1]     = { class_dollar_symc };
	ir_type *init_call_type   = get_entity_type(gcj_init_entity);
	ir_node *init_call        = new_r_Call(block, *mem, init_callee, 1, init_args, init_call_type);
	        *mem              = new_r_Proj(init_call, mode_M, pn_Call_M);
}

ir_node *gcji_allocate_object(ir_type *type, ir_graph *irg, ir_node *block, ir_node **mem)
{
	assert (is_Class_type(type));
	assert (gcji_is_api_class(type));

	ir_node *cur_mem = *mem;
	gcji_class_init(type, irg, block, &cur_mem);

	symconst_symbol alloc_sym;
	alloc_sym.entity_p = gcj_alloc_entity;
	ir_node *alloc_callee = new_r_SymConst(irg, mode_reference, alloc_sym, symconst_addr_ent);

	ir_entity *class_dollar_field = get_class_member_by_name(type, class_dollar_ident);
	assert (class_dollar_field); // this is availabe after the call to construct_class_init

	symconst_symbol class_dollar_sym;
	class_dollar_sym.entity_p   = class_dollar_field;
	ir_node *class_dollar_symc = new_r_SymConst(irg, mode_reference, class_dollar_sym, symconst_addr_ent);

	ir_node *alloc_args[1]   = { class_dollar_symc };
	ir_type *alloc_call_type = get_entity_type(gcj_alloc_entity);
	ir_node *alloc_call      = new_r_Call(block, cur_mem, alloc_callee, 1, alloc_args, alloc_call_type);
             cur_mem         = new_r_Proj(alloc_call, mode_M, pn_Call_M);
	ir_node *ress            = new_r_Proj(alloc_call, mode_T, pn_Call_T_result);
	ir_node *res             = new_r_Proj(ress, mode_reference, 0);

	*mem = cur_mem;
	return res;
}

ir_node *gcji_new_string(ir_entity *bytes, ir_graph *irg, ir_node *block, ir_node **mem)
{
	symconst_symbol callee_sym;
	callee_sym.entity_p = gcj_new_string_entity;
	ir_node *callee = new_r_SymConst(irg, mode_reference, callee_sym, symconst_addr_ent);

	symconst_symbol string_sym;
	string_sym.entity_p = bytes;
	ir_node *string_symc = new_r_SymConst(irg, mode_reference, string_sym, symconst_addr_ent);

	ir_node *args[1] = { string_symc };

	ir_node *cur_mem     = *mem;
	ir_type *callee_type = get_entity_type(gcj_new_string_entity);
	ir_node *call        = new_r_Call(block, cur_mem, callee, 1, args, callee_type);
	         cur_mem     = new_r_Proj(call, mode_M, pn_Call_M);
	ir_node *ress        = new_r_Proj(call, mode_T, pn_Call_T_result);
	ir_node *res         = new_r_Proj(ress, mode_reference, 0);

	*mem = cur_mem;
	return res;
}
