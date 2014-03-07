#include "gcj_interface.h"
#include "types.h"
#include "class_file.h"
#include "class_registry.h"

#include <libfirm/firm.h>
#include <liboo/oo.h>
#include <liboo/ddispatch.h>
#include <liboo/rtti.h>
#include <liboo/dmemory.h>
#include "adt/cpset.h"
#include "mangle.h"
#include "adt/obst.h"

#include <assert.h>
#include <string.h>

static ident     *class_dollar_ident;
static ir_type   *glob;
static ir_entity *gcj_alloc_entity;
static ir_entity *gcj_init_entity;
static ir_entity *gcj_new_string_entity;
static ir_entity *gcj_new_prim_array_entity;
static ir_entity *gcj_new_object_array_entity;
static ir_entity *gcj_abstract_method_entity;
static ir_entity *gcj_lookup_interface_entity;
static ir_entity *gcj_instanceof_entity;
static ir_entity *gcj_checkcast_entity;
static ir_entity *gcj_get_array_class_entity;
static ir_entity *gcj_new_multiarray_entity;

static ir_entity *gcj_boolean_rtti_entity;
static ir_entity *gcj_byte_rtti_entity;
static ir_entity *gcj_char_rtti_entity;
static ir_entity *gcj_short_rtti_entity;
static ir_entity *gcj_int_rtti_entity;
static ir_entity *gcj_long_rtti_entity;
static ir_entity *gcj_float_rtti_entity;
static ir_entity *gcj_double_rtti_entity;
static ir_entity *gcj_array_length;

static ir_mode *mode_ushort;
static ir_type *type_ushort;
static ir_type *type_method_desc;
static ir_type *type_field_desc;
static ir_type *type_utf8_const;
static ir_type *type_java_lang_object;
static ir_type *type_java_lang_class;

ident *subobject_ident;

extern char* strdup(const char* s);
static ir_entity *do_emit_utf8_const(const char *bytes, size_t len);
static ir_entity *emit_type_signature(ir_type *type);

static unsigned java_style_hash(const char* s)
{
	unsigned hash = 0;
	size_t len = strlen(s);
	for (size_t i = 0; i < len; i++) {
		// FIXME: this doesn't work for codepoints that are not ASCII.
		hash = (31 * hash) + s[i];
	}
	return hash;
}

static cpset_t scp; // static constant pool

typedef struct {
	char      *s;
	ir_entity *utf8c;
} scp_entry;

static int scp_cmp(const void *p1, const void *p2)
{
	const scp_entry *a = (const scp_entry*) p1;
	const scp_entry *b = (const scp_entry*) p2;

	return strcmp(a->s, b->s) == 0;
}

static unsigned scp_hash (const void *obj)
{
	const char *s = ((scp_entry*)obj)->s;
	return java_style_hash(s);
}

static void free_scpe(scp_entry *scpe)
{
	if (scpe == NULL)
		return;

	free(scpe->s);
	free(scpe);
}

static ir_entity *add_compound_member(ir_type *compound, const char *name,
                                      ir_type *type)
{
	ident *id = new_id_from_str(name);
	return new_entity(compound, id, type);
}

static ir_type *create_method_desc_type(void)
{
	ir_type *type = new_type_struct(new_id_from_str("method_desc"));
	add_compound_member(type, "name", type_reference);
	add_compound_member(type, "sig", type_reference);
	add_compound_member(type, "accflags", type_ushort);
	add_compound_member(type, "index", type_ushort);
	add_compound_member(type, "ncode", type_reference);
	add_compound_member(type, "throws", type_reference);
	default_layout_compound_type(type);
	return type;
}

static ir_type *create_field_desc_type(void)
{
	ir_type *type_offs_addr = new_type_union(new_id_from_str("offset/address"));
	add_compound_member(type_offs_addr, "boffset", type_int);
	add_compound_member(type_offs_addr, "addr", type_reference);
	default_layout_compound_type(type_offs_addr);

	ir_type *type = new_type_struct(new_id_from_str("field_desc"));
	add_compound_member(type, "name", type_reference);
	add_compound_member(type, "type", type_reference);
	add_compound_member(type, "accflags", type_ushort);
	add_compound_member(type, "bsize", type_ushort);
	add_compound_member(type, "boffset/address", type_offs_addr);
	default_layout_compound_type(type);
	return type;
}

static ir_type *create_utf8_const_type(void)
{
	ir_type *type_byte = get_type_for_mode(mode_Bu);
	ir_type *type_var_char_array = new_type_array(type_byte);
	set_array_variable_size(type_var_char_array, 1);

	ident *id = new_id_from_str("utf8_const");
	ir_type *type = new_type_struct(id);
	add_compound_member(type, "hash", type_ushort);
	add_compound_member(type, "len", type_ushort);
	add_compound_member(type, "data", type_var_char_array);
	set_compound_variable_size(type, 1);
	default_layout_compound_type(type);
	return type;
}

void gcji_create_array_type(void)
{
	ident   *id   = new_id_from_str("array");
	ir_type *type = new_type_class(id);
	assert(type_java_lang_object != NULL);
	add_compound_member(type, subobject_ident, type_java_lang_object);
	ident *length_id = new_id_from_str("length");
	gcj_array_length = add_compound_member(type, length_id, type_int);

	ir_type *data_array = new_type_array(type_byte);
	set_array_variable_size(data_array, 1);
	ident *data_id = new_id_from_str("data");
	add_compound_member(type, data_id, data_array);
	set_compound_variable_size(type, 1);

	default_layout_compound_type(type);
}

ir_entity *gcji_get_abstract_method_entity(void)
{
	return gcj_abstract_method_entity;
}

void gcji_add_java_lang_class_fields(ir_type *type)
{
	assert(type == type_java_lang_class);
	ir_type *ref_java_lang_class = new_type_pointer(type_java_lang_class);
	add_compound_member(type, "next_or_version", ref_java_lang_class);
	add_compound_member(type, "name", type_reference);
	add_compound_member(type, "accflags", type_ushort);
	add_compound_member(type, "superclass", ref_java_lang_class);
	add_compound_member(type, "constants.size", type_int);
	add_compound_member(type, "constants.tags", type_reference);
	add_compound_member(type, "constants.data", type_reference);
	add_compound_member(type, "methods", type_reference);
	add_compound_member(type, "method_count", type_short);
	add_compound_member(type, "vtable_method_count", type_short);
	add_compound_member(type, "fields", type_reference);
	add_compound_member(type, "size_in_bytes", type_int);
	add_compound_member(type, "field_count", type_short);
	add_compound_member(type, "static_field_count", type_short);
	add_compound_member(type, "vtable", type_reference);
	add_compound_member(type, "otable", type_reference);
	add_compound_member(type, "otable_syms", type_reference);
	add_compound_member(type, "atable", type_reference);
	add_compound_member(type, "atable_syms", type_reference);
	add_compound_member(type, "itable", type_reference);
	add_compound_member(type, "itable_syms", type_reference);
	add_compound_member(type, "catch_classes", type_reference);
	add_compound_member(type, "interfaces", type_reference);
	add_compound_member(type, "loader", type_reference);
	add_compound_member(type, "interface_count", type_short);
	add_compound_member(type, "state", type_byte);
	add_compound_member(type, "thread", type_reference);
	add_compound_member(type, "depth", type_short);
	add_compound_member(type, "ancestors", type_reference);
	add_compound_member(type, "idt", type_reference);
	add_compound_member(type, "arrayclass", type_reference);
	add_compound_member(type, "protectionDomain", type_reference);
	add_compound_member(type, "assertion_table", type_reference);
	add_compound_member(type, "hack_signers", type_reference);
	add_compound_member(type, "chain", type_reference);
	add_compound_member(type, "aux_info", type_reference);
	add_compound_member(type, "engine", type_reference);
	add_compound_member(type, "reflection_data", type_reference);
}

void gcji_create_vtable_entity(ir_type *type)
{
	const char *name         = get_class_name(type);
	ident      *vtable_ident = mangle_vtable_name(name);
	ir_type    *unknown      = get_unknown_type();
	ir_type    *glob         = get_glob_type();
	ir_entity  *vtable       = new_entity(glob, vtable_ident, unknown);
	oo_set_class_vtable_entity(type, vtable);
}

void gcji_set_java_lang_class(ir_type *type)
{
	assert(type_java_lang_class == NULL);
	type_java_lang_class = type;
}

void gcji_set_java_lang_object(ir_type *type)
{
	assert(type_java_lang_object == NULL);
	type_java_lang_object = type;
}

void gcji_class_init(ir_type *type, ir_node *block, ir_node **mem)
{
	assert(is_Class_type(type));

	ir_graph *irg         = get_irn_irg(block);
	ir_node  *init_callee = new_r_Address(irg, gcj_init_entity);

	ir_node *cur_mem        = *mem;
	ir_node *jclass         = gcji_get_runtime_classinfo(type, irg, block, &cur_mem);
	ir_node *init_args[1]   = { jclass };
	ir_type *init_call_type = get_entity_type(gcj_init_entity);
	ir_node *init_call      = new_r_Call(block, *mem, init_callee, 1, init_args, init_call_type);
	         cur_mem        = new_r_Proj(init_call, mode_M, pn_Call_M);

	*mem = cur_mem;
}

ir_node *gcji_allocate_object(ir_type *type, ir_node *block, ir_node **mem)
{
	assert(is_Class_type(type));

	ir_node *cur_mem = *mem;

	ir_graph *irg          = get_irn_irg(block);
	ir_node  *alloc_callee = new_r_Address(irg, gcj_alloc_entity);

	ir_node *jclass          = gcji_get_runtime_classinfo(type, irg, block, &cur_mem);
	ir_node *alloc_args[1]   = { jclass };
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
		ir_node *callee = new_r_Address(irg, gcj_new_prim_array_entity);

		ir_node *jclass      = gcji_get_runtime_classinfo(eltype, irg, block, &cur_mem);
		ir_node *args[2]     = { jclass, count };
		ir_type *callee_type = get_entity_type(gcj_new_prim_array_entity);
		ir_node *call        = new_r_Call(block, cur_mem, callee, 2, args, callee_type);
		         cur_mem     = new_r_Proj(call, mode_M, pn_Call_M);
		ir_node *ress        = new_r_Proj(call, mode_T, pn_Call_T_result);
		         res         = new_r_Proj(ress, mode_reference, 0);
	} else {
		ir_node *callee = new_r_Address(irg, gcj_new_object_array_entity);

		ir_node *jclass      = gcji_get_runtime_classinfo(eltype, irg, block, &cur_mem);
		ir_node *nullptr     = new_r_Const_long(irg, mode_reference, 0);
		ir_node *args[3]     = { count, jclass, nullptr };
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
	ir_node *callee      = new_r_Address(irg, gcj_new_string_entity);
	ir_node *string_symc = new_r_Address(irg, bytes);

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

ir_node *gcji_get_arraylength(dbg_info *dbgi, ir_node *block, ir_node *arrayref,
                              ir_node **mem)
{
	ir_graph *irg       = get_irn_irg(block);
	ir_node  *cur_mem   = *mem;
	ir_node  *addr      = new_r_simpleSel(block, get_irg_no_mem(irg),
	                                        arrayref, gcj_array_length);
	ir_node  *load      = new_rd_Load(dbgi, block, cur_mem, addr, mode_int,
	                                  cons_none);
	ir_node  *load_mem  = new_r_Proj(load, mode_M, pn_Load_M);
	ir_node  *res       = new_r_Proj(load, mode_int, pn_Load_res);

	*mem = load_mem;
	return res;
}

ir_node *gcji_get_arrayclass(ir_node *array_class_ref, ir_graph *irg, ir_node *block, ir_node **mem)
{
	ir_node *cur_mem = *mem;

	ir_node *callee      = new_r_Address(irg, gcj_get_array_class_entity);

	ir_node *args[]      = { array_class_ref, new_r_Const_long(irg, mode_reference, 0) };
	ir_type *callee_type = get_entity_type(gcj_get_array_class_entity);
	ir_node *call        = new_r_Call(block, cur_mem, callee, 2, args, callee_type);
	         cur_mem     = new_r_Proj(call, mode_M, pn_Call_M);
	ir_node *ress        = new_r_Proj(call, mode_T, pn_Call_T_result);
	ir_node *res         = new_r_Proj(ress, mode_reference, 0);

	*mem = cur_mem;
	return res;
}

ir_entity *gcji_emit_utf8_const(constant_t *constant, int mangle_slash)
{
	assert(constant->base.kind == CONSTANT_UTF8_STRING);
	constant_utf8_string_t *string_const = (constant_utf8_string_t*) constant;

	char *bytes = mangle_slash ? strdup(string_const->bytes) : string_const->bytes;

	if (mangle_slash)
	  for (char *p = bytes; *p != '\0'; p++)
		if (*p == '/') *p = '.';

	ir_entity *res = do_emit_utf8_const(bytes, string_const->length);

	if (mangle_slash)
		free(bytes);

	return res;
}

static void set_compound_init_null(ir_initializer_t *init, size_t idx)
{
	ir_initializer_t *null_init = get_initializer_null();
	set_initializer_compound_value(init, idx, null_init);
}

static void set_compound_init_node(ir_initializer_t *init, size_t idx,
								   ir_node *value)
{
	if (value == NULL) {
		set_compound_init_null(init, idx);
		return;
	}
	ir_initializer_t *value_init = create_initializer_const(value);
	set_initializer_compound_value(init, idx, value_init);
}

static ir_initializer_t *new_initializer_long(ir_mode *mode, long val)
{
	ir_tarval *tv = new_tarval_from_long(val, mode);
	return create_initializer_tarval(tv);
}

static void set_compound_init_num(ir_initializer_t *init, size_t idx,
                                  ir_mode *mode, long val)
{
	ir_initializer_t *value_init = new_initializer_long(mode, val);
	set_initializer_compound_value(init, idx, value_init);
}

static void set_compound_init_entref(ir_initializer_t *init, size_t idx,
                                     ir_entity *entity)
{
	if (entity == NULL) {
		set_compound_init_null(init, idx);
		return;
	}
	ir_graph *ccode = get_const_code_irg();
	ir_node  *node  = new_r_Address(ccode, entity);
	set_compound_init_node(init, idx, node);
}

static void setup_vtable(ir_type *cls, ir_initializer_t *initializer,
                         unsigned vtable_size)
{
	/*
	 * vtable layout (a la gcj)
	 *
	 * _ZTVNxyzE:
	 *   0
	 *   0
	 *   <vtable slot 0> _ZNxyz6class$E   (vptr points here)
	 *   <vtable slot 1> GC bitmap marking descriptor
	 *   <vtable slot 2> addr(first method)
	 *   ...
	 *   <vtable slot n> addr(last method)
	 */
	ir_entity *rtti      = oo_get_class_rtti_entity(cls);

	assert(vtable_size >= 4);
	set_compound_init_null(initializer, 0);
	set_compound_init_null(initializer, 1);
	set_compound_init_entref(initializer, 2, rtti);
	set_compound_init_null(initializer, 3);
}

static ir_entity *do_emit_utf8_const(const char *bytes, size_t len)
{
	size_t len0 = len + 1; // incl. the '\0' byte
	int hash = java_style_hash(bytes) & 0xFFFF;

	scp_entry test_scpe;
	test_scpe.s = (char*)bytes;

	scp_entry *found_scpe = cpset_find(&scp, &test_scpe);
	if (found_scpe != NULL) {
		ir_entity *utf8const = found_scpe->utf8c;
		assert(is_entity(utf8const));
		return utf8const;
	}

	ir_initializer_t *data_init = create_initializer_compound(len0);
	for (size_t i = 0; i < len0; ++i) {
		set_compound_init_num(data_init, i, mode_Bu, bytes[i]);
	}

	ir_initializer_t *cinit = create_initializer_compound(3);
	set_compound_init_num(cinit, 0, mode_ushort, hash);
	set_compound_init_num(cinit, 1, mode_ushort, len);
	set_initializer_compound_value(cinit, 2, data_init);

	ident     *id    = id_unique("_Utf8_%u_");
	ir_entity *utf8c = new_entity(get_glob_type(), id, type_utf8_const);
	set_entity_initializer(utf8c, cinit);
	set_entity_ld_ident(utf8c, id);
	add_entity_linkage(utf8c, IR_LINKAGE_CONSTANT);

	scp_entry *new_scpe = XMALLOC(scp_entry);
	new_scpe->s = XMALLOCN(char, len0);
	memcpy(new_scpe->s, bytes, len0);
	new_scpe->utf8c = utf8c;
	cpset_insert(&scp, new_scpe);

	return utf8c;
}

static ir_initializer_t *get_method_desc(ir_type *classtype, ir_entity *ent)
{
	class_t  *linked_class  = (class_t*)  oo_get_type_link(classtype);
	method_t *linked_method = (method_t*) oo_get_entity_link(ent);
	assert(linked_class && linked_method);

	constant_t *name_const     = linked_class->constants[linked_method->name_index];
	ir_entity  *name_const_ent = gcji_emit_utf8_const(name_const, 1);

	constant_t *desc_const     = linked_class->constants[linked_method->descriptor_index];
	ir_entity  *desc_const_ent = gcji_emit_utf8_const(desc_const, 1);

	uint16_t accflags = linked_method->access_flags | 0x4000; // 0x4000 is gcj specific, meaning ACC_TRANSLATED.
	uint16_t vtable_index = get_entity_vtable_number(ent);

	ir_entity *code
		= ((linked_method->access_flags & ACCESS_FLAG_ABSTRACT) != 0)
		? gcj_abstract_method_entity : ent;

	ir_initializer_t *cinit = create_initializer_compound(6);
	set_compound_init_entref(cinit, 0, name_const_ent);
	set_compound_init_entref(cinit, 1, desc_const_ent);
	set_compound_init_num(cinit, 2, mode_ushort, accflags);
	set_compound_init_num(cinit, 3, mode_ushort, vtable_index);
	set_compound_init_entref(cinit, 4, code);
	set_compound_init_null(cinit, 5); /* throws */
	return cinit;
}

static ir_entity *emit_method_table(ir_type *classtype)
{
	class_t *linked_class = (class_t*) oo_get_type_link(classtype);
	assert(linked_class);
	uint16_t n_methods = linked_class->n_methods;

	ir_type *array_type = new_type_array(type_method_desc);
	set_array_size_int(array_type, n_methods);
	unsigned size = n_methods * get_type_size_bytes(type_method_desc);
	set_type_size_bytes(array_type, size);

	ir_initializer_t *cinit = create_initializer_compound(n_methods);
	for (uint16_t i = 0; i < n_methods; i++) {
		ir_entity *method = linked_class->methods[i]->link;
		ir_initializer_t *method_desc = get_method_desc(classtype, method);
		set_initializer_compound_value(cinit, i, method_desc);
	}

	ident     *id     = id_unique("_MT_%u_");
	ir_entity *mt_ent = new_entity(get_glob_type(), id, array_type);
	set_entity_initializer(mt_ent, cinit);
	set_entity_ld_ident(mt_ent, id);

	return mt_ent;
}

#define MUX_PRIM_TYPES(typevar, action_boolean, action_byte, action_char, action_short, action_int, action_long, action_float, action_double) \
		do { \
	      if (typevar == type_boolean) { action_boolean; } \
		  else if (typevar == type_byte) { action_byte; } \
		  else if (typevar == type_char) { action_char; } \
		  else if (typevar == type_short) { action_short; } \
		  else if (typevar == type_int) { action_int; } \
		  else if (typevar == type_long) { action_long; } \
		  else if (typevar == type_float) { action_float; } \
		  else if (typevar == type_double) { action_double; } \
		} while (0);

ir_entity *gcji_get_rtti_entity(ir_type *type)
{
	if (is_Pointer_type(type)) {
		ir_type *points_to = get_pointer_points_to_type(type);
		if (is_Class_type(points_to))
			return oo_get_class_rtti_entity(points_to);
		return NULL;
	} else if (is_Primitive_type(type)) {
		MUX_PRIM_TYPES(type,
				return gcj_boolean_rtti_entity,
				return gcj_byte_rtti_entity,
				return gcj_char_rtti_entity,
				return gcj_short_rtti_entity,
				return gcj_int_rtti_entity,
				return gcj_long_rtti_entity,
				return gcj_float_rtti_entity,
				return gcj_double_rtti_entity);
		return NULL;
	} else {
		return oo_get_class_rtti_entity(type);
	}
}

static ir_initializer_t *get_field_desc(ir_type *classtype, ir_entity *ent)
{
	class_t *linked_class = (class_t*) oo_get_type_link(classtype);
	field_t *linked_field = (field_t*) oo_get_entity_link(ent);
	assert(linked_class && linked_field);

	constant_t *name_const = linked_class->constants[linked_field->name_index];
	ir_entity  *name_ent   = gcji_emit_utf8_const(name_const, 1);

	ir_type   *field_type  = get_entity_type(ent);
	ir_entity *rtti_entity = gcji_get_rtti_entity(field_type);
	if (rtti_entity == NULL) {
		rtti_entity = emit_type_signature(field_type);
	}

	ir_graph *ccode = get_const_code_irg();

	ir_node *bsize = new_r_Size(ccode, mode_ushort, field_type);

	ir_initializer_t *offset_addr_init = create_initializer_compound(2);
	if (linked_field->access_flags & ACCESS_FLAG_STATIC) {
		set_compound_init_null(offset_addr_init, 0);
		set_compound_init_entref(offset_addr_init, 1, ent);
	} else {
		ir_node *offset = new_r_Offset(ccode, mode_int, ent);
		set_compound_init_node(offset_addr_init, 0, offset);
		set_compound_init_null(offset_addr_init, 1);
	}

	unsigned NUM_FIELDS = 5;
	ir_initializer_t *init = create_initializer_compound(NUM_FIELDS);
	size_t f = 0;
	set_compound_init_entref(init, f++, name_ent);
	set_compound_init_entref(init, f++, rtti_entity);
	set_compound_init_num(init, f++, mode_ushort, linked_field->access_flags);
	set_compound_init_node(init, f++, bsize);
	set_initializer_compound_value(init, f++, offset_addr_init);
	assert(f == NUM_FIELDS);
	return init;
}

static ir_entity *emit_field_table(ir_type *classtype)
{
	class_t *linked_class = (class_t*) oo_get_type_link(classtype);
	assert(linked_class);
	uint16_t n_fields = linked_class->n_fields;
	if (n_fields == 0)
		return NULL;

	ir_type *type_array = new_type_array(type_field_desc);
	set_array_size_int(type_array, n_fields);
	unsigned size = n_fields * get_type_size_bytes(type_field_desc);
	set_type_size_bytes(type_array, size);

	ir_initializer_t *init = create_initializer_compound(n_fields);
	for (uint16_t i = 0; i < n_fields; i++) {
		ir_entity        *field = linked_class->fields[i]->link;
		ir_initializer_t *desc  = get_field_desc(classtype, field);
		set_initializer_compound_value(init, i, desc);
	}

	ident     *id     = id_unique("_FT_%u_");
	ir_entity *ft_ent = new_entity(get_glob_type(), id, type_array);
	set_entity_initializer(ft_ent, init);
	set_entity_ld_ident(ft_ent, id);
	return ft_ent;
}

static ir_entity *emit_interface_table(ir_type *classtype)
{
	class_t *linked_class = (class_t*) oo_get_type_link(classtype);
	assert(linked_class);
	uint16_t n_interfaces = linked_class->n_interfaces;
	if (n_interfaces == 0)
		return NULL;

	ir_type *type_array = new_type_array(type_reference);
	set_array_size_int(type_array, n_interfaces);
	unsigned size = n_interfaces * get_type_size_bytes(type_reference);
	set_type_size_bytes(type_array, size);

	ir_initializer_t *init = create_initializer_compound(n_interfaces);
	for (uint16_t i = 0; i < n_interfaces; i++) {
		uint16_t                iface_ref = linked_class->interfaces[i];
		constant_classref_t    *clsref    = (constant_classref_t*)    linked_class->constants[iface_ref];
		constant_utf8_string_t *clsname   = (constant_utf8_string_t*) linked_class->constants[clsref->name_index];

		ir_type    *type = class_registry_get(clsname->bytes);
		assert(type);
		ir_entity  *rtti_entity = gcji_get_rtti_entity(type);
		assert(rtti_entity != NULL);
		set_compound_init_entref(init, i, rtti_entity);
	}

	ident     *id     = id_unique("_IF_%u_");
	ir_entity *if_ent = new_entity(get_glob_type(), id, type_array);
	set_entity_initializer(if_ent, init);
	set_entity_ld_ident(if_ent, id);
	return if_ent;
}

void gcji_create_rtti_entity(ir_type *type)
{
	const char *name = get_class_name(type);
	/* create RTTI object entity (actual initializer is constructed in liboo) */
	ident     *rtti_ident  = mangle_rtti_name(name);
	ir_type   *unknown     = get_unknown_type();
	ir_entity *rtti_entity = new_entity(glob, rtti_ident, unknown);
	oo_set_class_rtti_entity(type, rtti_entity);
}

static void add_pointer_in_jcr_segment(ir_entity *entity)
{
	ir_type   *segment = get_segment_type(IR_SEGMENT_JCR);
	ident     *id  = id_unique("jcr_ptr.%u");
	ir_entity *ptr = new_entity(segment, id, type_reference);
	ir_graph  *irg = get_const_code_irg();
	ir_node   *val = new_r_Address(irg, entity);

	set_entity_ld_ident(ptr, new_id_from_chars("", 0));
	set_entity_compiler_generated(ptr, 1);
	set_entity_visibility(ptr, ir_visibility_private);
	set_entity_linkage(ptr, IR_LINKAGE_CONSTANT|IR_LINKAGE_HIDDEN_USER);
	set_entity_alignment(ptr, 1);
	set_atomic_ent_value(ptr, val);
}

static ir_node *get_vtable_ref(ir_type *type)
{
	ir_entity *cls_vtable = oo_get_class_vtable_entity(type);
	if (cls_vtable == NULL)
		return NULL;
	ir_graph *ccode = get_const_code_irg();
	ir_node  *addr  = new_r_Address(ccode, cls_vtable);
	unsigned offset
		= ddispatch_get_vptr_points_to_index() * get_mode_size_bytes(mode_reference);
	ir_node *cnst  = new_r_Const_long(ccode, mode_reference, offset);
	ir_node *block = get_r_cur_block(ccode);
	ir_node *add   = new_r_Add(block, addr, cnst, mode_reference);
	return add;
}

void gcji_setup_rtti_entity(class_t *cls, ir_type *type)
{
	ir_entity *rtti_entity = oo_get_class_rtti_entity(type);
	assert(type_java_lang_class != NULL);
	set_entity_type(rtti_entity, type_java_lang_class);

	ir_entity *name_ent = gcji_emit_utf8_const(
			cls->constants[cls->constants[cls->this_class]->classref.name_index], 1);

	uint16_t   accflags     = cls->access_flags;
	ir_entity *method_table = emit_method_table(type);
	ir_entity *field_table  = emit_field_table(type);

	ir_graph *ccode = get_const_code_irg();
	ir_node  *size_in_bytes	= new_r_Size(ccode, mode_int, type);

	int16_t field_count = cls->n_fields;
	int16_t static_field_count = 0;
	for (int16_t i = 0; i < field_count; i++) {
		if ((cls->fields[i]->access_flags & ACCESS_FLAG_STATIC) != 0)
			++static_field_count;
	}

	ir_entity *superclass_rtti = NULL;
	if (get_class_n_supertypes(type) > 0) {
		ir_type *superclass = get_class_supertype(type, 0);
		/* TODO: search for first non-interface instead of taking the
		 * first one and hoping it is a non-interface */
		assert(!oo_get_class_is_interface(superclass));

		superclass_rtti = gcji_get_rtti_entity(superclass);
	}

	ir_node *vtable_init = get_vtable_ref(type);

	int16_t interface_count = cls->n_interfaces;
	ir_entity *interfaces = emit_interface_table(type);

	// w/o slots 0=rtti. see lower_oo.c
	int16_t vtable_method_count = oo_get_class_vtable_size(type)
		- (ddispatch_get_index_of_first_method() - ddispatch_get_vptr_points_to_index());

	ir_node *class_vtable = get_vtable_ref(type_java_lang_class);

	/* initializer for base class java.lang.Object */
	ir_initializer_t *base_init = create_initializer_compound(1);
	set_compound_init_node(base_init, 0, class_vtable);

	/* initializer for java.lang.Class */
	unsigned NUM_FIELDS = 39;
	ir_initializer_t *init = create_initializer_compound(NUM_FIELDS);
	size_t f = 0;
	set_initializer_compound_value(init, f++, base_init);
	set_compound_init_num(init, f++, mode_P, 407000);
	set_compound_init_entref(init, f++, name_ent);
	set_compound_init_num(init, f++, mode_ushort, accflags);
	set_compound_init_entref(init, f++, superclass_rtti);
	// _Jv_Constants inlined. not sure if this is needed for compiled code.
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);

	set_compound_init_entref(init, f++, method_table);
	set_compound_init_num(init, f++, mode_short, cls->n_methods);
	set_compound_init_num(init, f++, mode_short, vtable_method_count);

	set_compound_init_entref(init, f++, field_table);
	set_compound_init_node(init, f++, size_in_bytes);
	set_compound_init_num(init, f++, mode_short, field_count);
	set_compound_init_num(init, f++, mode_short, static_field_count);
	set_compound_init_node(init, f++, vtable_init);

	// most of the following fields are set at runtime during the class linking.
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);

	set_compound_init_entref(init, f++, interfaces);
	set_compound_init_null(init, f++);
	set_compound_init_num(init, f++, mode_short, interface_count);

	set_compound_init_num(init, f++, mode_byte, 1); // state

	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++);
	set_compound_init_null(init, f++); // engine
	set_compound_init_null(init, f++);
	assert(f == NUM_FIELDS);

	set_entity_initializer(rtti_entity, init);
	set_entity_alignment(rtti_entity, 32);

	add_pointer_in_jcr_segment(rtti_entity);
}

static ir_entity *emit_type_signature(ir_type *type)
{
	ir_type *curtype = type;
	unsigned n_pointer_levels = 0;

	while (is_Pointer_type(curtype)) {
		n_pointer_levels++;
		curtype = get_pointer_points_to_type(curtype);
	}

	struct obstack obst;
	obstack_init(&obst);

	if (n_pointer_levels > 0) {
		for (unsigned i = 0; i < n_pointer_levels-1; i++)
			obstack_1grow(&obst, '[');

		if (is_Primitive_type(curtype))
			obstack_1grow(&obst, '[');
	}

	if (is_Primitive_type(curtype)) {
		char c;
		MUX_PRIM_TYPES(curtype, c='Z', c='B', c='C', c='S', c='I', c='J', c='F', c='D');
		obstack_1grow(&obst, c);
	} else {
		assert(is_Class_type(curtype));
		obstack_1grow(&obst, 'L');
		obstack_printf(&obst, "%s", get_class_name(curtype));
		obstack_1grow(&obst, ';');
		obstack_1grow(&obst, '\0');
	}
	const char *sig_bytes = obstack_finish(&obst);

	ir_entity *res = do_emit_utf8_const(sig_bytes, strlen(sig_bytes));

	obstack_free(&obst, NULL);
	return res;
}

ir_node *gcji_get_runtime_classinfo(ir_type *type, ir_graph *irg, ir_node *block, ir_node **mem)
{
	ir_entity *rtti_entity = gcji_get_rtti_entity(type);
	if (rtti_entity != NULL) {
		return new_r_Address(irg, rtti_entity);
	}

	/* Arrays are represented as pointer types. We extract the base type,
	 * get its classinfo and let gcj give the array type for that.
	 *
	 * gcj emits the type signature to the class' constant pool. During
	 * class linking, the reference to the utf8const is replaced by the
	 * reference to the appropriate class object.
	 */

	assert(is_Pointer_type(type));

	unsigned n_pointer_levels = 0;
	ir_type *eltype = type;
	while (is_Pointer_type(eltype)) {
		n_pointer_levels++;
		eltype = get_pointer_points_to_type(eltype);
	}

	if (!is_Primitive_type(eltype)) n_pointer_levels--;

	ir_entity *elem_cdf = gcji_get_rtti_entity(eltype);
	assert(elem_cdf != NULL);
	ir_node *array_class_ref = new_r_Address(irg, elem_cdf);

	ir_node *cur_mem = *mem;

	for (unsigned d = 0; d < n_pointer_levels; d++) {
		array_class_ref = gcji_get_arrayclass(array_class_ref, irg, block, &cur_mem);
	}

	*mem = cur_mem;

	return array_class_ref;
}

static ir_entity *get_vptr_entity(void)
{
	ir_type *type = class_registry_get("java/lang/Object");
	assert(type != NULL);
	return oo_get_class_vptr_entity(type);
}

ir_node *gcji_lookup_interface(ir_node *objptr, ir_type *iface, ir_entity *method, ir_graph *irg, ir_node *block, ir_node **mem)
{
	ir_node   *cur_mem       = *mem;

	// we need the reference to the object's class$ field
	// first, dereference the vptr in order to get the vtable address.
	ir_entity *vptr_entity   = get_vptr_entity();
	ir_node   *vptr_addr     = new_r_Sel(block, new_r_NoMem(irg), objptr, 0, NULL, vptr_entity);
	ir_node   *vptr_load     = new_r_Load(block, cur_mem, vptr_addr, mode_reference, cons_none);
	ir_node   *vtable_addr   = new_r_Proj(vptr_load, mode_reference, pn_Load_res);
	           cur_mem       = new_r_Proj(vptr_load, mode_M, pn_Load_M);

	// second, dereference vtable_addr (it points to the slot where the address of the class$ field is stored).
	ir_node   *cd_load       = new_r_Load(block, cur_mem, vtable_addr, mode_reference, cons_none);
	ir_node   *cd_ref        = new_r_Proj(cd_load, mode_reference, pn_Load_res);
	           cur_mem       = new_r_Proj(cd_load, mode_M, pn_Load_M);

	class_t   *linked_class  = (class_t*)  oo_get_type_link(iface);
	method_t  *linked_method = (method_t*) oo_get_entity_link(method);
	assert(linked_class && linked_method);

	constant_t *name_const   = linked_class->constants[linked_method->name_index];
	ir_entity *name_const_ent= gcji_emit_utf8_const(name_const, 1);
	ir_node   *name_ref      = new_r_Address(irg, name_const_ent);

	constant_t *desc_const   = linked_class->constants[linked_method->descriptor_index];
	ir_entity *desc_const_ent= gcji_emit_utf8_const(desc_const, 1);
	ir_node   *desc_ref      = new_r_Address(irg, desc_const_ent);

	ir_node   *callee        = new_r_Address(irg, gcj_lookup_interface_entity);

	ir_node   *args[3]       = { cd_ref, name_ref, desc_ref };
	ir_type   *call_type     = get_entity_type(gcj_lookup_interface_entity);
	ir_node   *call          = new_r_Call(block, cur_mem, callee, 3, args, call_type);
	           cur_mem       = new_r_Proj(call, mode_M, pn_Call_M);
	ir_node   *ress          = new_r_Proj(call, mode_T, pn_Call_T_result);
	ir_node   *res           = new_r_Proj(ress, mode_reference, 0);

	*mem = cur_mem;

	return res;
}

ir_node *gcji_instanceof(ir_node *objptr, ir_type *classtype, ir_graph *irg, ir_node *block, ir_node **mem)
{
	ir_node   *cur_mem = *mem;
	ir_node   *jclass = gcji_get_runtime_classinfo(classtype, irg, block, &cur_mem);

	ir_type   *instanceof_type = get_entity_type(gcj_instanceof_entity);
	ir_node   *callee = new_r_Address(irg, gcj_instanceof_entity);
	ir_node   *args[] = { objptr, jclass };

	ir_node   *call = new_r_Call(block, cur_mem, callee, 2, args, instanceof_type);
	           cur_mem = new_r_Proj(call, mode_M, pn_Call_M);
	ir_node   *ress = new_r_Proj(call, mode_T, pn_Call_T_result);
	ir_node   *call_res = new_r_Proj(ress, mode_int, 0);
	ir_node   *zero = new_r_Const(irg, get_mode_null(mode_int));
	ir_node   *res  = new_r_Cmp(block, call_res, zero, ir_relation_less_greater);

	*mem = cur_mem;
	return res;
}

void gcji_checkcast(ir_type *classtype, ir_node *objptr, ir_graph *irg, ir_node *block, ir_node **mem)
{
	ir_node   *cur_mem = *mem;
	ir_node   *jclass = gcji_get_runtime_classinfo(classtype, irg, block, &cur_mem);

	ir_type   *instanceof_type = get_entity_type(gcj_checkcast_entity);
	ir_node   *callee = new_r_Address(irg, gcj_checkcast_entity);
	ir_node   *args[] = { jclass, objptr };

	ir_node   *call = new_r_Call(block, cur_mem, callee, 2, args, instanceof_type);
	cur_mem = new_r_Proj(call, mode_M, pn_Call_M);

	*mem = cur_mem;
}

static ir_node *alloc_dims_array(unsigned dims, ir_node **sizes, ir_graph *irg, ir_node *block, ir_node **mem)
{
	ir_node *cur_mem = *mem;

	ir_mode        *dim_mode = mode_ushort;
	const unsigned  bytes    = dims * (get_mode_size_bits(dim_mode) / 8);

	ir_node *dims_const = new_r_Const_long(irg, dim_mode, bytes);
	ir_node *alloc = new_r_Alloc(block, cur_mem, dims_const, 1);
	ir_node *arr   = new_r_Proj(alloc, mode_reference, pn_Alloc_res);
	cur_mem = new_r_Proj(alloc, mode_M, pn_Alloc_M);

	ir_entity *elem_ent = get_array_element_entity(type_array_int);

	for (unsigned d = 0; d < dims; d++) {
		ir_node *index_const = new_r_Const_long(irg, mode_int, d);
		ir_node *in[] = { index_const };
		ir_node *sel = new_r_Sel(block, new_r_NoMem(irg), arr, 1, in, elem_ent);
		ir_node *store = new_r_Store(block, cur_mem, sel, sizes[d], cons_none);
		cur_mem = new_r_Proj(store, mode_M, pn_Store_M);
	}

	*mem = cur_mem;

	return arr;
}

ir_node *gcji_new_multiarray(ir_node *array_class_ref, unsigned dims, ir_node **sizes, ir_graph *irg, ir_node *block, ir_node **mem)
{
	ir_node *cur_mem = *mem;

	ir_node *callee      = new_r_Address(irg, gcj_new_multiarray_entity);
	ir_node *dims_arr    = alloc_dims_array(dims, sizes, irg, block, &cur_mem);
	ir_node *args[]      = { array_class_ref, new_r_Const_long(irg, mode_int, dims), dims_arr };
	ir_type *callee_type = get_entity_type(gcj_new_multiarray_entity);
	ir_node *call        = new_r_Call(block, cur_mem, callee, 3, args, callee_type);
	         cur_mem     = new_r_Proj(call, mode_M, pn_Call_M);
	ir_node *ress        = new_r_Proj(call, mode_T, pn_Call_T_result);
	ir_node *res         = new_r_Proj(ress, mode_reference, 0);

	*mem = cur_mem;
	return res;
}

static void dummy(ir_type *t)
{
	(void)t;
}

void gcji_init()
{
	class_dollar_ident = new_id_from_str("class$");
	glob               = get_glob_type();

	ir_type *t_ptr  = new_type_primitive(mode_reference);
	ir_type *t_size = new_type_primitive(mode_Iu);

	// gcj_alloc
	ir_type *gcj_alloc_method_type = new_type_method(1, 1);
	set_method_param_type(gcj_alloc_method_type, 0, t_ptr);
	set_method_res_type(gcj_alloc_method_type, 0, t_ptr);
	set_method_additional_properties(gcj_alloc_method_type, mtp_property_malloc);

	ident *gcj_alloc_id = new_id_from_str("_Jv_AllocObjectNoFinalizer");
	gcj_alloc_entity = new_entity(glob, gcj_alloc_id, gcj_alloc_method_type);
	set_entity_visibility(gcj_alloc_entity, ir_visibility_external);

	// gcj_init
	ir_type *gcj_init_method_type = new_type_method(1, 0);
	set_method_param_type(gcj_init_method_type, 0, t_ptr);

	ident *gcj_init_id = new_id_from_str("_Jv_InitClass");
	gcj_init_entity = new_entity(glob, gcj_init_id, gcj_init_method_type);
	set_entity_visibility(gcj_init_entity, ir_visibility_external);

	// gcj_new_string
	ir_type *gcj_new_string_method_type = new_type_method(1, 1);
	set_method_param_type(gcj_new_string_method_type, 0, t_ptr);
	set_method_res_type(gcj_new_string_method_type, 0, t_ptr);

	ident *gcj_new_string_id = new_id_from_str("_Z22_Jv_NewStringUtf8ConstP13_Jv_Utf8Const");
	gcj_new_string_entity = new_entity(glob, gcj_new_string_id, gcj_new_string_method_type);
	set_entity_visibility(gcj_new_string_entity, ir_visibility_external);

	// gcj_new_prim_array
	ir_type *gcj_new_prim_array_method_type = new_type_method(2, 1);
	set_method_param_type(gcj_new_prim_array_method_type, 0, t_ptr);
	set_method_param_type(gcj_new_prim_array_method_type, 1, t_size);
	set_method_res_type(gcj_new_prim_array_method_type, 0, t_ptr);

	ident *gcj_new_prim_array_id = new_id_from_str("_Jv_NewPrimArray");
	gcj_new_prim_array_entity = new_entity(glob, gcj_new_prim_array_id, gcj_new_prim_array_method_type);
	set_entity_visibility(gcj_new_prim_array_entity, ir_visibility_external);

	// gcj_new_object_array
	ir_type *gcj_new_object_array_method_type = new_type_method(3, 1);
	set_method_param_type(gcj_new_object_array_method_type, 0, t_size);
	set_method_param_type(gcj_new_object_array_method_type, 1, t_ptr);
	set_method_param_type(gcj_new_object_array_method_type, 2, t_ptr);
	set_method_res_type(gcj_new_object_array_method_type, 0, t_ptr);

	ident *gcj_new_object_array_id = new_id_from_str("_Jv_NewObjectArray");
	gcj_new_object_array_entity = new_entity(glob, gcj_new_object_array_id, gcj_new_object_array_method_type);
	set_entity_visibility(gcj_new_object_array_entity, ir_visibility_external);

	// gcji_abstract_method
	ir_type *gcj_abstract_method_type = new_type_method(0, 0);
	gcj_abstract_method_entity = new_entity(glob, new_id_from_str("_Jv_ThrowAbstractMethodError"), gcj_abstract_method_type);
	set_entity_visibility(gcj_abstract_method_entity, ir_visibility_external);

	// gcji_lookup_interface
	ir_type *gcj_lookup_interface_type = new_type_method(3,1);
	set_method_param_type(gcj_lookup_interface_type, 0, t_ptr);
	set_method_param_type(gcj_lookup_interface_type, 1, t_ptr);
	set_method_param_type(gcj_lookup_interface_type, 2, t_ptr);
	set_method_res_type(gcj_lookup_interface_type, 0, t_ptr);
	gcj_lookup_interface_entity = new_entity(glob, new_id_from_str("_Jv_LookupInterfaceMethod"), gcj_lookup_interface_type);
	set_entity_visibility(gcj_lookup_interface_entity, ir_visibility_external);

	// gcji_instanceof
	ir_type *gcj_instanceof_type = new_type_method(2,1);
	set_method_param_type(gcj_instanceof_type, 0, type_reference);
	set_method_param_type(gcj_instanceof_type, 1, type_reference);
	set_method_res_type(gcj_instanceof_type, 0, type_int);
	gcj_instanceof_entity = new_entity(glob, new_id_from_str("_Jv_IsInstanceOf"), gcj_instanceof_type);
	set_entity_visibility(gcj_instanceof_entity, ir_visibility_external);

	// gcji_checkcast
	ir_type *gcj_checkcast_type = new_type_method(2,0);
	set_method_param_type(gcj_checkcast_type, 0, type_reference);
	set_method_param_type(gcj_checkcast_type, 1, type_reference);
	gcj_checkcast_entity = new_entity(glob, new_id_from_str("_Jv_CheckCast"), gcj_checkcast_type);
	set_entity_visibility(gcj_checkcast_entity, ir_visibility_external);

	// gcji_get_array_class
	ir_type *gcj_get_array_class_type = new_type_method(2,1);
	set_method_param_type(gcj_get_array_class_type, 0, type_reference);
	set_method_param_type(gcj_get_array_class_type, 1, type_reference);
	set_method_res_type(gcj_get_array_class_type, 0, type_reference);
	gcj_get_array_class_entity = new_entity(glob, new_id_from_str("_Z17_Jv_GetArrayClassPN4java4lang5ClassEPNS0_11ClassLoaderE"), gcj_get_array_class_type);
	set_entity_visibility(gcj_get_array_class_entity, ir_visibility_external);

	// gcji_new_multi_array
	ir_type *gcj_new_multiarray_type = new_type_method(3,1);
	set_method_param_type(gcj_new_multiarray_type, 0, type_reference);
	set_method_param_type(gcj_new_multiarray_type, 1, type_int);
	set_method_param_type(gcj_new_multiarray_type, 2, type_reference); // XXX: actually int[]
	set_method_res_type(gcj_new_multiarray_type, 0, type_reference);
	gcj_new_multiarray_entity = new_entity(glob, new_id_from_str("_Z17_Jv_NewMultiArrayPN4java4lang5ClassEiPi"), gcj_new_multiarray_type);
	set_entity_visibility(gcj_new_multiarray_entity, ir_visibility_external);

	// primitive classes
	gcj_boolean_rtti_entity= new_entity(glob, new_id_from_str("_Jv_booleanClass"), type_reference);
	gcj_byte_rtti_entity   = new_entity(glob, new_id_from_str("_Jv_byteClass"), type_reference);
	gcj_char_rtti_entity   = new_entity(glob, new_id_from_str("_Jv_charClass"), type_reference);
	gcj_short_rtti_entity  = new_entity(glob, new_id_from_str("_Jv_shortClass"), type_reference);
	gcj_int_rtti_entity    = new_entity(glob, new_id_from_str("_Jv_intClass"), type_reference);
	gcj_long_rtti_entity   = new_entity(glob, new_id_from_str("_Jv_longClass"), type_reference);
	gcj_float_rtti_entity  = new_entity(glob, new_id_from_str("_Jv_floatClass"), type_reference);
	gcj_double_rtti_entity = new_entity(glob, new_id_from_str("_Jv_doubleClass"), type_reference);
	set_entity_visibility(gcj_boolean_rtti_entity, ir_visibility_external);
	set_entity_visibility(gcj_byte_rtti_entity, ir_visibility_external);
	set_entity_visibility(gcj_char_rtti_entity, ir_visibility_external);
	set_entity_visibility(gcj_short_rtti_entity, ir_visibility_external);
	set_entity_visibility(gcj_int_rtti_entity, ir_visibility_external);
	set_entity_visibility(gcj_long_rtti_entity, ir_visibility_external);
	set_entity_visibility(gcj_float_rtti_entity, ir_visibility_external);
	set_entity_visibility(gcj_double_rtti_entity, ir_visibility_external);

	mode_ushort = new_int_mode("US", irma_twos_complement, 16, 0, 16);
	type_ushort = new_type_primitive(mode_ushort);

	cpset_init(&scp, scp_hash, scp_cmp);

	type_method_desc = create_method_desc_type();
	type_field_desc  = create_field_desc_type();
	type_utf8_const  = create_utf8_const_type();

	subobject_ident  = new_id_from_str("@base");

	ddispatch_set_vtable_layout(2, 4, 2, setup_vtable);
	ddispatch_set_abstract_method_ident(new_id_from_str("_Jv_ThrowAbstractMethodError"));
	ddispatch_set_interface_lookup_constructor(gcji_lookup_interface);

	/* we construct rtti right away */
	rtti_set_runtime_typeinfo_constructor(dummy);
	rtti_set_instanceof_constructor(gcji_instanceof);

	dmemory_set_allocation_methods(gcji_get_arraylength);
}

void gcji_deinit()
{
	cpset_iterator_t iter;
	cpset_iterator_init(&iter, &scp);

	scp_entry *cur_scpe;
	while ( (cur_scpe = (scp_entry*)cpset_iterator_next(&iter)) != NULL) {
		free_scpe(cur_scpe);
	}

	cpset_destroy(&scp);
}
