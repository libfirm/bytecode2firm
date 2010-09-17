
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
static ir_entity *gcj_new_prim_array_entity;
static ir_entity *gcj_new_object_array_entity;

static ir_entity *gcj_byteClass_entity;
static ir_entity *gcj_charClass_entity;
static ir_entity *gcj_shortClass_entity;
static ir_entity *gcj_intClass_entity;
static ir_entity *gcj_longClass_entity;
static ir_entity *gcj_floatClass_entity;
static ir_entity *gcj_doubleClass_entity;

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
		|| strncmp("gnu/", classname, 4)   == 0
		|| strncmp("sun/", classname, 4)   == 0;
}

void gcji_init()
{
	class_dollar_ident = new_id_from_str("class$");
	glob               = get_glob_type();

	ir_type *t_ptr     = new_type_primitive(mode_reference);
	ir_type *t_size    = new_type_primitive(mode_Iu);

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

	// gcj_new_prim_array
	ir_type *gcj_new_prim_array_method_type = new_type_method(2, 1);
	set_method_param_type(gcj_new_prim_array_method_type, 0, t_ptr);
	set_method_param_type(gcj_new_prim_array_method_type, 1, t_size);
	set_method_res_type(gcj_new_prim_array_method_type, 0, t_ptr);

	ident   *gcj_new_prim_array_id = new_id_from_str("_Jv_NewPrimArray");
	gcj_new_prim_array_entity = new_entity(glob, gcj_new_prim_array_id, gcj_new_prim_array_method_type);
	set_entity_visibility(gcj_new_prim_array_entity, ir_visibility_external);

	// gcj_new_object_array
	ir_type *gcj_new_object_array_method_type = new_type_method(3, 1);
	set_method_param_type(gcj_new_object_array_method_type, 0, t_size);
	set_method_param_type(gcj_new_object_array_method_type, 1, t_ptr);
	set_method_param_type(gcj_new_object_array_method_type, 2, t_ptr);
	set_method_res_type(gcj_new_object_array_method_type, 0, t_ptr);

	ident   *gcj_new_object_array_id = new_id_from_str("_Jv_NewObjectArray");
	gcj_new_object_array_entity = new_entity(glob, gcj_new_object_array_id, gcj_new_object_array_method_type);
	set_entity_visibility(gcj_new_object_array_entity, ir_visibility_external);

	// primitive classes
	gcj_byteClass_entity   = new_entity(glob, new_id_from_str("_Jv_byteClass"), type_reference);
	gcj_charClass_entity   = new_entity(glob, new_id_from_str("_Jv_charClass"), type_reference);
	gcj_shortClass_entity  = new_entity(glob, new_id_from_str("_Jv_shortClass"), type_reference);
	gcj_intClass_entity    = new_entity(glob, new_id_from_str("_Jv_intClass"), type_reference);
	gcj_longClass_entity   = new_entity(glob, new_id_from_str("_Jv_longClass"), type_reference);
	gcj_floatClass_entity  = new_entity(glob, new_id_from_str("_Jv_floatClass"), type_reference);
	gcj_doubleClass_entity = new_entity(glob, new_id_from_str("_Jv_doubleClass"), type_reference);
	set_entity_visibility(gcj_byteClass_entity, ir_visibility_external);
	set_entity_visibility(gcj_charClass_entity, ir_visibility_external);
	set_entity_visibility(gcj_shortClass_entity, ir_visibility_external);
	set_entity_visibility(gcj_intClass_entity, ir_visibility_external);
	set_entity_visibility(gcj_longClass_entity, ir_visibility_external);
	set_entity_visibility(gcj_floatClass_entity, ir_visibility_external);
	set_entity_visibility(gcj_doubleClass_entity, ir_visibility_external);
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

ir_node *gcji_allocate_array(ir_type *eltype, ir_node *count, ir_graph *irg, ir_node *block, ir_node **mem)
{
	ir_node   *res        = NULL;
	ir_node   *cur_mem    = *mem;

	if (is_Primitive_type(eltype)) {
		ir_entity *jclass_ref = NULL;

		     if (eltype == type_byte)   jclass_ref = gcj_byteClass_entity;
		else if (eltype == type_char)   jclass_ref = gcj_charClass_entity;
		else if (eltype == type_short)  jclass_ref = gcj_shortClass_entity;
		else if (eltype == type_int)    jclass_ref = gcj_intClass_entity;
		else if (eltype == type_long)   jclass_ref = gcj_longClass_entity;
		else if (eltype == type_float)  jclass_ref = gcj_floatClass_entity;
		else if (eltype == type_double) jclass_ref = gcj_doubleClass_entity;
		else assert (0);

		symconst_symbol callee_sym;
		callee_sym.entity_p = gcj_new_prim_array_entity;
		ir_node *callee = new_r_SymConst(irg, mode_reference, callee_sym, symconst_addr_ent);

		symconst_symbol jclass_sym;
		jclass_sym.entity_p = jclass_ref;
		ir_node *jclass = new_r_SymConst(irg, mode_reference, jclass_sym, symconst_addr_ent);

		ir_node *args[2] = { jclass, count };


		ir_type *callee_type = get_entity_type(gcj_new_prim_array_entity);
		ir_node *call        = new_r_Call(block, cur_mem, callee, 2, args, callee_type);
		         cur_mem     = new_r_Proj(call, mode_M, pn_Call_M);
		ir_node *ress        = new_r_Proj(call, mode_T, pn_Call_T_result);
		         res         = new_r_Proj(ress, mode_reference, 0);
	} else {
		assert (is_Class_type(eltype));
		assert (gcji_is_api_class(eltype)); // FIXME: instances of my classes would require a correct class$ field

		ir_entity *class_dollar_field = get_class_member_by_name(eltype, class_dollar_ident);
		if (class_dollar_field == NULL) {
			class_dollar_field = add_class_dollar_field_recursive(eltype);
		}

		symconst_symbol callee_sym;
		callee_sym.entity_p = gcj_new_object_array_entity;
		ir_node *callee = new_r_SymConst(irg, mode_reference, callee_sym, symconst_addr_ent);

		symconst_symbol jclass_sym;
		jclass_sym.entity_p = class_dollar_field;
		ir_node *jclass = new_r_SymConst(irg, mode_reference, jclass_sym, symconst_addr_ent);

		ir_node *nullptr = new_Const_long(mode_reference, 0);

		ir_node *args[3] = { count, jclass, nullptr };


		ir_type *callee_type = get_entity_type(gcj_new_object_array_entity);
		ir_node *call        = new_r_Call(block, cur_mem, callee, 3, args, callee_type);
		         cur_mem     = new_r_Proj(call, mode_M, pn_Call_M);
		ir_node *ress        = new_r_Proj(call, mode_T, pn_Call_T_result);
		         res         = new_r_Proj(ress, mode_reference, 0);
	}

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

ir_node *gcji_get_arraylength(ir_node *arrayref, ir_graph *irg, ir_node *block, ir_node **mem)
{
	(void) irg;
	ir_node *cur_mem = *mem;

	ir_node *length_offset = new_Const_long(mode_reference, GCJI_LENGTH_OFFSET); // in gcj, arrays are subclasses of java/lang/Object. "length" is the second field.
	ir_node *length_addr = new_r_Add(block, arrayref, length_offset, mode_reference);
	ir_node *length_load = new_r_Load(block, cur_mem, length_addr, mode_int, cons_none);
	         cur_mem     = new_r_Proj(length_load, mode_M, pn_Load_M);
	ir_node *res         = new_r_Proj(length_load, mode_int, pn_Load_res);

	*mem = cur_mem;
	return res;
}
