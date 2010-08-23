#include "class_file.h"
#include "opcodes.h"
#include "lower_oo.h"
#include "types.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "adt/error.h"
#include "adt/obst.h"
#include "adt/array.h"
#include "adt/raw_bitset.h"
#include "adt/pdeq.h"
#include "adt/cpmap.h"
#include "adt/hashptr.h"
#include "adt/xmalloc.h"

#include "mangle.h"

#include <libfirm/firm.h>

#define VERBOSE

static pdeq    *worklist;
static class_t *class_file;
static const char *main_class_name;
static const char *main_class_name_short;

static constant_t *get_constant(uint16_t index)
{
	assert(index < class_file->n_constants);
	constant_t *constant = class_file->constants[index];
	assert(constant != NULL);
	return constant;
}

static const char *get_constant_string(uint16_t index)
{
	const constant_t *constant = get_constant(index);
	assert(constant->kind == CONSTANT_UTF8_STRING);
	return constant->utf8_string.bytes;
}

static ir_type *get_class_type(const char *name);
static ir_type *get_classref_type(uint16_t index);

ir_mode *mode_byte;
ir_mode *mode_char;
ir_mode *mode_short;
ir_mode *mode_int;
ir_mode *mode_long;
ir_mode *mode_float;
ir_mode *mode_double;
ir_mode *mode_reference;

ir_type *type_byte;
ir_type *type_char;
ir_type *type_short;
ir_type *type_int;
ir_type *type_long;
ir_type *type_boolean;
ir_type *type_float;
ir_type *type_double;
ir_type *type_reference;

ir_type *type_array_byte_boolean;
ir_type *type_array_char;
ir_type *type_array_short;
ir_type *type_array_int;
ir_type *type_array_long;
ir_type *type_array_float;
ir_type *type_array_double;
ir_type *type_array_reference;

ident     *vptr_ident;
ir_entity *builtin_arraylength;

static void init_types(void)
{
	mode_byte
		= new_ir_mode("B", irms_int_number, 8, 1, irma_twos_complement, 32);
	type_byte = new_type_primitive(mode_byte);

	mode_char
		= new_ir_mode("C", irms_int_number, 16, 0, irma_twos_complement, 0);
	type_char = new_type_primitive(mode_char);

	mode_short
		= new_ir_mode("S", irms_int_number, 16, 1, irma_twos_complement, 32);
	type_short = new_type_primitive(mode_short);

	mode_int
		= new_ir_mode("I", irms_int_number, 32, 1, irma_twos_complement, 32);
	type_int = new_type_primitive(mode_int);

	mode_long
		= new_ir_mode("J", irms_int_number, 64, 1, irma_twos_complement, 64);
	type_long = new_type_primitive(mode_long);

	ir_mode *mode_boolean = mode_byte;
	type_boolean = new_type_primitive(mode_boolean);

	mode_float
		= new_ir_mode("F", irms_float_number, 32, 1, irma_ieee754, 0);
	type_float = new_type_primitive(mode_float);

	mode_double
		= new_ir_mode("D", irms_float_number, 64, 1, irma_ieee754, 0);
	type_double = new_type_primitive(mode_double);

	mode_reference = mode_P;

	type_array_byte_boolean = new_type_array(1, type_byte);
	type_array_short        = new_type_array(1, type_short);
	type_array_char         = new_type_array(1, type_char);
	type_array_int          = new_type_array(1, type_int);
	type_array_long         = new_type_array(1, type_long);
	type_array_float        = new_type_array(1, type_float);
	type_array_double       = new_type_array(1, type_double);

	type_reference          = new_type_primitive(mode_reference);
	type_array_reference    = new_type_array(1, type_reference);

	vptr_ident              = new_id_from_str("vptr");

	ir_type *arraylength_type = new_type_method(1, 1);
	set_method_param_type(arraylength_type, 0, type_array_reference);
	set_method_res_type(arraylength_type, 0, type_int);
	set_method_additional_property(arraylength_type, mtp_property_pure);

	ir_type *global_type    = get_glob_type();
	ident   *arraylength_id = new_id_from_str("$builtin_arraylength");
	builtin_arraylength     = new_entity(global_type, arraylength_id,
	                                     arraylength_type);
	set_entity_additional_property(builtin_arraylength,
	                               mtp_property_intrinsic|mtp_property_private);
}

static cpmap_t class_registry;

static int class_registry_keys_equal(const void *p1, const void *p2)
{
	const char *s1 = (const char *) p1;
	const char *s2 = (const char *) p2;
	return strcmp(s1, s2) == 0;
}

static unsigned class_registry_key_hash(const void *p)
{
	const char *s = (const char *)p;
	return firm_fnv_hash_str(s);
}

static void class_registry_init(void)
{
	cpmap_init(&class_registry, class_registry_key_hash,
	           class_registry_keys_equal);
}

static ir_type *class_registry_get(const char *classname)
{
	ir_type *type = cpmap_find(&class_registry, classname);
	if (type == NULL) {
		ident *id = new_id_from_str(classname);
		type      = new_type_class(id);

		cpmap_set(&class_registry, classname, type);
	}

	return type;
}

static ir_type *descriptor_to_type(const char **descriptor);

static ir_type *array_to_type(const char **descriptor)
{
	ir_type *element_type = descriptor_to_type(descriptor);
	ir_type *result       = new_type_pointer(element_type);
	return result;
}

static ir_type *class_ref_to_type(const char **descriptor)
{
	const char *begin = *descriptor;
	const char *end;
	for (end = begin + 1; *end != ';'; ++end) {
		if (*end == '\0')
			panic("type descriptor '%s' misses ';' after 'L'\n", *descriptor);
	}
	*descriptor = end+1;

	ident   *id   = new_id_from_chars(begin, end-begin);
	ir_type *type = get_class_type(get_id_str(id));

	return new_type_pointer(type);
}

static ir_type *method_descriptor_to_type(const char *descriptor,
                                          ir_type *owner, uint16_t method_flags)
{
	const char *p = descriptor;
	ir_type **arguments = NEW_ARR_F(ir_type*, 0);

	/* add implicit "this" parameter if not static */
	if ((method_flags & ACCESS_FLAG_STATIC) == 0) {
		ir_type *this_type = new_type_pointer(owner);
		ARR_APP1(ir_type*, arguments, this_type);
	}

	if (*p != '(') {
		panic("method descriptor '%s' does not start with '('\n", descriptor);
	}
	++p;

	while (*p != ')') {
		if (*p == '\0')
			panic("method type descriptor '%s' misses closing ')'\n",
			      descriptor);
		ir_type *arg_type = descriptor_to_type(&p);
		ARR_APP1(ir_type*, arguments, arg_type);
	}
	p++;

	ir_type *res_types[1];
	int      n_res;
	if (*p == 'V') {
		p++;
		n_res = 0;
	} else {
		n_res        = 1;
		res_types[0] = descriptor_to_type(&p);
	}
	if (*p != '\0') {
		panic("invalid method descriptor '%s': extra chars at end",
		      descriptor);
	}

	ir_type *method_type = new_type_method(ARR_LEN(arguments), n_res);
	for (int i = 0; i < ARR_LEN(arguments); ++i) {
		set_method_param_type(method_type, i, arguments[i]);
	}
	for (int r = 0; r < n_res; ++r) {
		set_method_res_type(method_type, r, res_types[r]);
	}
	DEL_ARR_F(arguments);

	return method_type;
}

static ir_type *descriptor_to_type(const char **descriptor)
{
	char c = **descriptor;
	(*descriptor)++;
	switch (c) {
	case 'B': return type_byte;
	case 'C': return type_char;
	case 'S': return type_short;
	case 'I': return type_int;
	case 'J': return type_long;
	case 'Z': return type_boolean;
	case 'D': return type_double;
	case 'F': return type_float;
	case 'L': return class_ref_to_type(descriptor);
	case '[': return array_to_type(descriptor);
	}
	panic("Invalid type descriptor '%s'\n", *descriptor);
}

static ir_type *complete_descriptor_to_type(const char *descriptor)
{
	const char **p      = &descriptor;
	ir_type     *result = descriptor_to_type(p);
	if (**p != '\0') {
		panic("invalid type descriptor '%s' (extra characters at end)\n",
		      descriptor);
	}
	return result;
}

static void create_field_entity(field_t *field, ir_type *owner)
{
	const char *name       = get_constant_string(field->name_index);
	const char *descriptor = get_constant_string(field->descriptor_index);
	ir_type    *type       = complete_descriptor_to_type(descriptor);
	ident      *id         = new_id_from_str(name);
	ir_entity  *entity     = new_entity(owner, id, type);
	set_entity_link(entity, field);

	if (field->access_flags & ACCESS_FLAG_STATIC) {
		set_entity_allocation(entity, allocation_static);
	}

	ident *mangled_id      = mangle_entity_name(entity, id);
	set_entity_ld_ident(entity, mangled_id);

#ifdef VERBOSE
	fprintf(stderr, "Field %s\n", name);
#endif
}

static const attribute_code_t *code;
static uint16_t                stack_pointer;
static uint16_t                max_locals;

static ir_mode *get_arith_mode(ir_mode *mode)
{
	if (mode_is_int(mode)
	 && mode != mode_int
	 && mode != mode_long
    ) {
		return mode_int;
	}
	return mode;
}

/**
 * transform value into value with arithmetic mode
 * (= all integer calulations are done in mode_int so we transform integer
 *  value not in mode_int to it)
 */
static ir_node *get_arith_value(ir_node *node)
{
	ir_mode *irn_mode   = get_irn_mode(node);
	ir_mode *arith_mode = get_arith_mode(irn_mode);
	if (irn_mode != arith_mode)
		node = new_Conv(node, arith_mode);
	return node;
}

static bool needs_two_slots(ir_mode *mode)
{
	return mode == mode_long || mode == mode_double;
}

static void symbolic_push(ir_node *node)
{
	if (stack_pointer >= code->max_stack)
		panic("code exceeds stack limit");
	ir_mode *mode = get_irn_mode(node);
	assert (mode == NULL || (mode == get_arith_mode(mode)));

	/* double and long need 2 stackslots */
	if (needs_two_slots(mode))
		set_value(stack_pointer++, new_Bad());

	set_value(stack_pointer++, node);
}

static ir_node *symbolic_pop(ir_mode *mode)
{
	assert (mode == NULL || (mode == get_arith_mode(mode)));

	if (stack_pointer == 0)
		panic("code produces stack underflow");

	if (mode == NULL) {
		mode = ir_guess_mode(stack_pointer-1);
		assert (mode != NULL);
	}

	ir_node *result = get_value(--stack_pointer, mode);

	/* double and long need 2 stackslots */
	if (needs_two_slots(mode)) {
		get_value(--stack_pointer, mode);
	}

	return result;
}

static void set_local(uint16_t n, ir_node *node)
{
	assert (n < max_locals);
	set_value(code->max_stack + n, node);
	ir_mode *mode = get_irn_mode(node);
	assert (mode == NULL || mode == get_arith_mode(mode));

	if (needs_two_slots(mode)) {
		assert (n+1 < max_locals);
		set_value(code->max_stack + n+1, new_Bad());
	}
}

static ir_node *get_local(uint16_t n, ir_mode *mode)
{
	// the Bad nodes can mystically become Phi nodes...
//	if (needs_two_slots(mode)) {
//		assert(n+1 < max_locals);
//		ir_node *dummy = get_value(code->max_stack + n+1, mode);
//		(void) dummy;
//		assert(is_Bad(dummy));
//	}
	assert (n < max_locals);
	assert (mode == NULL || mode == get_arith_mode(mode));
	return get_value(code->max_stack + n, mode);
}

static ir_node *create_symconst(ir_entity *entity)
{
	union symconst_symbol sym;
	sym.entity_p = entity;
	return new_SymConst(mode_reference, sym, symconst_addr_ent);
}

static ir_entity *find_entity(ir_type *classtype, ident *id)
{
	assert (is_Class_type(classtype));

	// 1. is the entity defined in this class?
	ir_entity *entity = get_class_member_by_name(classtype, id);

	// 2. is the entity defined in the superclass?
	int n_superclasses = get_class_n_supertypes(classtype);
	if (entity == NULL && n_superclasses > 0) {
		assert (n_superclasses == 1);
		ir_type *supertype = get_class_supertype(classtype, 0);
		entity = find_entity(supertype, id);
	}

	// 3. is the entity defined in an interface?
	class_t *cls = (class_t*) get_type_link(classtype);
	if (entity == NULL && cls->n_interfaces > 0) {
		// the current class_file is managed like a stack. See: get_class_type(..)
		class_t *old = class_file;
		class_file = cls;

		for (uint16_t i = 0; i < cls->n_interfaces && entity == NULL; i++) {
			uint16_t interface_ref = cls->interfaces[i];
			ir_type *interface = get_classref_type(interface_ref);
			assert (interface != NULL);
			entity = find_entity(interface, id);
		}

		assert (class_file == cls);
		class_file = old;
	}

	return entity;
}

static ir_entity *get_method_entity(uint16_t index)
{
	constant_t *methodref = get_constant(index);
	if (methodref->kind != CONSTANT_METHODREF) {
		panic("get_method_entity index argument not a methodref");
	}
	ir_entity *entity = methodref->base.link;
	if (entity == NULL) {
		const constant_t *name_and_type 
			= get_constant(methodref->methodref.name_and_type_index);
		if (name_and_type->kind != CONSTANT_NAMEANDTYPE) {
			panic("invalid name_and_type in method %u", index);
		}
		ir_type *classtype 
			= get_classref_type(methodref->methodref.class_index);

		if (! is_Class_type(classtype)) {
			classtype = get_class_type("java/lang/Object"); // FIXME: need real arraytypes
		}

		const char *methodname
			= get_constant_string(name_and_type->name_and_type.name_index);
		ident *methodid = new_id_from_str(methodname);
		const char *descriptor
			= get_constant_string(name_and_type->name_and_type.descriptor_index);
		ident *descriptorid = new_id_from_str(descriptor);
		ident *name         = id_mangle_dot(methodid, descriptorid);

		entity = find_entity(classtype, name);
		assert (entity && is_method_entity(entity));
		methodref->base.link = entity;
	}

	return entity;
}

static ir_entity *get_interface_entity(uint16_t index)
{
	constant_t *interfacemethodref = get_constant(index);
	if (interfacemethodref->kind != CONSTANT_INTERFACEMETHODREF) {
		panic("get_method_entity index argument not an interfacemethodref");
	}
	ir_entity *entity = interfacemethodref->base.link;
	if (entity == NULL) {
		const constant_t *name_and_type
			= get_constant(interfacemethodref->interfacemethodref.name_and_type_index);
		if (name_and_type->kind != CONSTANT_NAMEANDTYPE) {
			panic("invalid name_and_type in interface method %u", index);
		}

		ir_type *classtype
			= get_classref_type(interfacemethodref->interfacemethodref.class_index);

		const char *methodname
			= get_constant_string(name_and_type->name_and_type.name_index);
		ident *methodid = new_id_from_str(methodname);

		const char *descriptor
			= get_constant_string(name_and_type->name_and_type.descriptor_index);

		ident *descriptorid = new_id_from_str(descriptor);
		ident *name         = id_mangle_dot(methodid, descriptorid);

		entity = find_entity(classtype, name);
		assert (entity && is_method_entity(entity));
		interfacemethodref->base.link = entity;
	}

	return entity;
}

static ir_entity *get_field_entity(uint16_t index)
{
	constant_t *fieldref = get_constant(index);
	if (fieldref->kind != CONSTANT_FIELDREF) {
		panic("get_field_entity index argumetn not a fieldref");
	}
	ir_entity *entity = fieldref->base.link;
	if (entity == NULL) {
		const constant_t *name_and_type 
			= get_constant(fieldref->fieldref.name_and_type_index);
		if (name_and_type->kind != CONSTANT_NAMEANDTYPE) {
			panic("invalid name_and_type in field %u", index);
		}
		ir_type *classtype 
			= get_classref_type(fieldref->fieldref.class_index);

		/* TODO: we could have a method with the same name */
		const char *fieldname
			= get_constant_string(name_and_type->name_and_type.name_index);
		ident *fieldid = new_id_from_str(fieldname);
		entity = find_entity(classtype, fieldid);
		assert (entity && !is_method_entity(entity));
		fieldref->base.link = entity;
	}

	return entity;
}

typedef struct basic_block_t {
	uint16_t  pc;
	ir_node  *block;
	int       stack_pointer; /**< stack pointer at beginning of block,
							      may be -1 if unknown */
} basic_block_t;

static int compare_basic_blocks(const void *d1, const void *d2)
{
	const basic_block_t *basic_block1 = (const basic_block_t*) d1;
	const basic_block_t *basic_block2 = (const basic_block_t*) d2;
	if (basic_block1->pc < basic_block2->pc) return -1;
	if (basic_block1->pc > basic_block2->pc) return  1;
	return 0;
}

static size_t         n_basic_blocks;
static basic_block_t *basic_blocks;

static basic_block_t *find_basic_block(uint16_t pc, size_t left, size_t right)
{
	if (left >= right)
		return NULL;

	size_t         middle      = left + (right - left) / 2;
	basic_block_t *basic_block = &basic_blocks[middle];
	
	if (pc < basic_block->pc)
		return find_basic_block(pc, left, middle);
	if (pc > basic_block->pc)
		return find_basic_block(pc, middle+1, right);

	if (basic_block->pc == pc)
		return basic_block;
	return NULL;
}

static basic_block_t *get_basic_block(uint16_t pc)
{
	basic_block_t *basic_block = find_basic_block(pc, 0, n_basic_blocks);
	assert(basic_block != NULL);
	return basic_block;
}

static ir_node *get_target_block_remember_stackpointer(uint16_t pc)
{
	basic_block_t *basic_block = get_basic_block(pc);
	if (basic_block->stack_pointer >= 0 &&
			basic_block->stack_pointer != stack_pointer) {
		panic("stack pointers don't match for PC %u\n", pc);
	}
	basic_block->stack_pointer = stack_pointer;

	return basic_block->block;
}

static void construct_cond(uint16_t pc_true, uint16_t pc_false,
                           ir_node *condition)
{
	ir_node *block_true  = get_target_block_remember_stackpointer(pc_true);
	ir_node *block_false = get_target_block_remember_stackpointer(pc_false);

	ir_node *cond = new_Cond(condition);
	ir_node *proj_true = new_Proj(cond, mode_X, pn_Cond_true);
	add_immBlock_pred(block_true, proj_true);
	ir_node *proj_false = new_Proj(cond, mode_X, pn_Cond_false);
	add_immBlock_pred(block_false, proj_false);

	set_cur_block(NULL);
}

static ir_node *simple_new_Div(ir_node *left, ir_node *right, ir_mode *mode)
{
	ir_node *mem     = get_store();
	ir_node *div     = new_Div(mem, left, right, mode, op_pin_state_pinned);
	ir_node *new_mem = new_Proj(div, mode_M, pn_Div_M);
	set_store(new_mem);
	return new_Proj(div, mode, pn_Div_res);
}

static ir_node *simple_new_Quot(ir_node *left, ir_node *right, ir_mode *mode)
{
	ir_node *mem     = get_store();
	ir_node *div     = new_Quot(mem, left, right, mode, op_pin_state_pinned);
	ir_node *new_mem = new_Proj(div, mode_M, pn_Div_M);
	set_store(new_mem);
	return new_Proj(div, mode, pn_Div_res);
}

static ir_node *simple_new_Mod(ir_node *left, ir_node *right, ir_mode *mode)
{
	ir_node *mem     = get_store();
	ir_node *div     = new_Mod(mem, left, right, mode, op_pin_state_pinned);
	ir_node *new_mem = new_Proj(div, mode_M, pn_Div_M);
	set_store(new_mem);
	return new_Proj(div, mode, pn_Div_res);
}

static void construct_arith(ir_mode *mode,
		ir_node *(*construct_func)(ir_node *, ir_node *, ir_mode *))
{
	ir_node *right  = symbolic_pop(mode);
	ir_node *left   = symbolic_pop(mode);
	ir_node *result = construct_func(left, right, mode);
	symbolic_push(result);
}

static void construct_shift_arith(ir_mode *mode,
		ir_node *(*construct_func)(ir_node *, ir_node *, ir_mode *))
{
	ir_node *right  = symbolic_pop(mode_int);
	ir_node *left   = symbolic_pop(mode);
	ir_node *right_u = new_Conv(right, mode_Iu);
	ir_node *result = construct_func(left, right_u, mode);
	symbolic_push(result);
}

static void construct_arith_unop(ir_mode *mode,
		ir_node *(*construct_func)(ir_node *, ir_mode *))
{
	ir_node *value  = symbolic_pop(mode);
	ir_node *result = construct_func(value, mode);
	symbolic_push(result);
}

static void push_const(ir_mode *mode, long val)
{
	ir_node *cnst = new_Const_long(mode, val);
	symbolic_push(cnst);
}

static void push_const_tarval(tarval *tv)
{
	ir_node *cnst = new_Const(tv);
	symbolic_push(cnst);
}

static void push_local(int idx, ir_mode *mode)
{
	ir_node *value = get_local(idx, mode);
	symbolic_push(value);
}

static void pop_set_local(int idx, ir_mode *mode)
{
	ir_node *value = symbolic_pop(mode);
	set_local(idx, value);
}

/*
 * Creates an entity initialized to the given string.
 * The string is written bytewise.
 */
static ir_entity *string_to_firm(const char *bytes, size_t length)
{
	ir_mode   *element_mode = mode_Bu;
	ir_type   *element_type = new_type_primitive(element_mode);
	ir_type   *array_type   = new_type_array(1, element_type);

    ident     *id           = id_unique("str_%u");
    ir_type   *global_type  = get_glob_type();
    ir_entity *entity       = new_entity(global_type, id, array_type);
    set_entity_ld_ident(entity, id);
    set_entity_visibility(entity, ir_visibility_private);
    add_entity_linkage(entity, IR_LINKAGE_CONSTANT);

    set_array_lower_bound_int(array_type, 0, 0);
    set_array_upper_bound_int(array_type, 0, length);
    set_type_size_bytes(array_type, length);
    set_type_state(array_type, layout_fixed);

    // initialize each array element to an input byte
    ir_initializer_t *initializer = create_initializer_compound(length);
    for (size_t i = 0; i < length; ++i) {
        tarval           *tv  = new_tarval_from_long(bytes[i], element_mode);
        ir_initializer_t *val = create_initializer_tarval(tv);
        set_initializer_compound_value(initializer, i, val);
    }

    set_entity_initializer(entity, initializer);

    return entity;
}

static ir_node *new_string_literal(const char* bytes, size_t length)
{
	ir_type *java_lang_String = get_class_type("java/lang/String");
	assert (java_lang_String != NULL);

	// allocate String instance
	ir_node   *mem       = get_store();
	ir_node   *alloc     = new_Alloc(mem, new_Const_long(mode_Iu, 1), java_lang_String, heap_alloc);
	ir_node   *res       = new_Proj(alloc, mode_reference, pn_Alloc_res);
	ir_node   *new_mem   = new_Proj(alloc, mode_M, pn_Alloc_M);
	set_store(new_mem);

	// create string const
	ir_entity *string_constant = string_to_firm(bytes, length);
	ir_node   *string_symc = create_symconst(string_constant);

	// call constructor
	ir_node *args[2];
	args[0] = res;
	args[1] = string_symc;

	ir_entity *ctor      = get_class_member_by_name(java_lang_String, new_id_from_str("<init>.([C)V"));
	ir_node   *ctor_symc = create_symconst(ctor);
	ir_type   *ctor_type = get_entity_type(ctor);
	assert (ctor != NULL);

	           mem       = get_store();
	ir_node   *call      = new_Call(mem, ctor_symc, 2, args, ctor_type);
	           new_mem   = new_Proj(call, mode_M, pn_Call_M);
	set_store(new_mem);

	return res;
}

static void push_load_const(uint16_t index)
{
	const constant_t *constant = get_constant(index);
	switch (constant->kind) {
	case CONSTANT_INTEGER:
		push_const(mode_int, (int32_t) constant->integer.value);
		break;
	case CONSTANT_FLOAT: {
		float    val   = *((float*) &constant->floatc.value);
		tarval  *tv    = new_tarval_from_double(val, mode_float);
		push_const_tarval(tv);
		break;
	}
	case CONSTANT_LONG: {
		char buf[128];
		uint64_t val = ((uint64_t)constant->longc.high_bytes << 32) | constant->longc.low_bytes;
		snprintf(buf, sizeof(buf), "%lld", (int64_t) val);
		tarval *tv = new_tarval_from_str(buf, strlen(buf), mode_long);
		push_const_tarval(tv);
		break;
	}
	case CONSTANT_DOUBLE: {
		// FIXME: need real implementation
		tarval *tv = new_tarval_from_double(1.0, mode_double);
		push_const_tarval(tv);
		break;
	}
	case CONSTANT_STRING: {
		constant_t *utf8_const = get_constant(constant->string.string_index);
		ir_node *string_literal = new_string_literal(utf8_const->utf8_string.bytes, utf8_const->utf8_string.length);
		symbolic_push(string_literal);
		break;
	}
	case CONSTANT_CLASSREF: {
		// FIXME: need real implementation
		push_const(mode_reference, 0);
		break;
	}
	default:
		panic("ldc without int, float or string constant");
	}
}

static void construct_vreturn(ir_type *method_type, ir_mode *mode)
{
	int      n_ins = 0;
	ir_node *in[1];

	if (mode != NULL) {
		ir_type *return_type = get_method_res_type(method_type, 0);
		ir_mode *res_mode    = get_type_mode(return_type);
		ir_node *val         = symbolic_pop(mode);
		n_ins = 1;
		in[0] = new_Conv(val, res_mode);
	}

	ir_node *ret   = new_Return(get_store(), n_ins, in);

	if (stack_pointer != 0) {
		fprintf(stderr, "Warning: stackpointer >0 after return at\n");
	}
	
	ir_node *end_block = get_irg_end_block(current_ir_graph);
	add_immBlock_pred(end_block, ret);
	set_cur_block(NULL);
}

static void construct_dup(void)
{
	uint16_t sp = stack_pointer;
	ir_node *val1 = symbolic_pop(NULL);
	assert (! needs_two_slots(get_irn_mode(val1)));

	symbolic_push(val1);
	symbolic_push(val1);
	assert ((sp+1) == stack_pointer);
}

static void construct_dup_x1(void)
{
	uint16_t sp = stack_pointer;
	ir_node *val1 = symbolic_pop(NULL);
	ir_node *val2 = symbolic_pop(NULL);
	assert (! needs_two_slots(get_irn_mode(val1)));
	assert (! needs_two_slots(get_irn_mode(val2)));

	symbolic_push(val1);
	symbolic_push(val2);
	symbolic_push(val1);
	assert ((sp+1) == stack_pointer);
}

static void construct_dup_x2(void)
{
	uint16_t sp = stack_pointer;
	ir_node *val1 = symbolic_pop(NULL);
	ir_node *val2 = symbolic_pop(NULL);
	ir_node *val3 = NULL;

	assert (! needs_two_slots(get_irn_mode(val1)));
	if (! needs_two_slots(get_irn_mode(val2))) {
		val3 = symbolic_pop(NULL);
		assert (! needs_two_slots(get_irn_mode(val3)));
	}
	symbolic_push(val1);
	if (val3 != NULL) symbolic_push(val3);
	symbolic_push(val2);
	symbolic_push(val1);
	assert ((sp+1) == stack_pointer);
}

static void construct_dup2(void)
{
	uint16_t sp = stack_pointer;
	ir_node *val1 = symbolic_pop(NULL);
	ir_node *val2 = NULL;
	if (! needs_two_slots(get_irn_mode(val1))) {
		val2 = symbolic_pop(NULL);
		assert (! needs_two_slots(get_irn_mode(val2)));
	}
	if (val2 != NULL) symbolic_push(val2);
	symbolic_push(val1);
	if (val2 != NULL) symbolic_push(val2);
	symbolic_push(val1);

	assert ((sp+2) == stack_pointer);
}

static void construct_dup2_x1(void)
{
	uint16_t sp = stack_pointer;
	ir_node *val1 = symbolic_pop(NULL);
	ir_node *val2 = symbolic_pop(NULL);
	ir_node *val3 = NULL;

	assert (! needs_two_slots(get_irn_mode(val2)));
	if (! needs_two_slots(get_irn_mode(val1))) {
		val3 = symbolic_pop(NULL);
		assert (! needs_two_slots(get_irn_mode(val3)));
	}
	if (val3 != NULL) symbolic_push(val2);
	symbolic_push(val1);
	if (val3 != NULL) symbolic_push(val3);
	symbolic_push(val2);
	symbolic_push(val1);
	assert ((sp+2) == stack_pointer);
}

static void construct_dup2_x2(void)
{
	uint16_t sp = stack_pointer;
	ir_node *val1 = symbolic_pop(NULL);
	ir_node *val2 = symbolic_pop(NULL);
	ir_node *val3 = NULL;
	ir_node *val4 = NULL;

	ir_mode *m1 = get_irn_mode(val1);
	ir_mode *m2 = get_irn_mode(val2);
	ir_mode *m3 = NULL;
	ir_mode *m4 = NULL;

	if (needs_two_slots(m1) && needs_two_slots(m2)) {
		// FORM 4
		symbolic_push(val1);
		symbolic_push(val2);
		symbolic_push(val1);
		return;
	}

	val3 = symbolic_pop(NULL);
	m3   = get_irn_mode(val3);
	if (!needs_two_slots(m1) && !needs_two_slots(m2) && needs_two_slots(m3)) {
		// FORM 3
		symbolic_push(val2);
		symbolic_push(val1);
		symbolic_push(val3);
		symbolic_push(val2);
		symbolic_push(val1);
		return;
	}

	if (needs_two_slots(m1) && !needs_two_slots(m2) && !needs_two_slots(m3)) {
		// FORM 2
		symbolic_push(val1);
		symbolic_push(val3);
		symbolic_push(val2);
		symbolic_push(val1);
		return;
	}

	val4 = symbolic_pop(NULL);
	m4   = get_irn_mode(val4);
	assert (!needs_two_slots(m1)
	     && !needs_two_slots(m2)
	     && !needs_two_slots(m3)
	     && !needs_two_slots(m4));
	// FORM 1
	symbolic_push(val2);
	symbolic_push(val1);
	symbolic_push(val4);
	symbolic_push(val3);
	symbolic_push(val2);
	symbolic_push(val1);

	assert ((sp+2) == stack_pointer);
}

static void construct_array_load(ir_type *array_type)
{
	ir_node   *index     = symbolic_pop(mode_int);
	ir_node   *base_addr = symbolic_pop(mode_reference);
	ir_node   *in[1]     = { index };
	ir_entity *entity    = get_array_element_entity(array_type);
	ir_node   *addr      = new_Sel(new_NoMem(), base_addr, 1, in, entity);

	ir_node   *mem       = get_store();
	ir_type   *type      = get_entity_type(entity);
	ir_mode   *mode      = get_type_mode(type);
	ir_node   *load      = new_Load(mem, addr, mode, cons_none);
	ir_node   *new_mem   = new_Proj(load, mode_M, pn_Load_M);
	ir_node   *value     = new_Proj(load, mode, pn_Load_res);
	set_store(new_mem);

	value = get_arith_value(value);
	symbolic_push(value);
}

static void construct_array_store(ir_type *array_type)
{
	ir_entity *entity    = get_array_element_entity(array_type);
	ir_type   *type      = get_entity_type(entity);
	ir_mode   *mode      = get_type_mode(type);
	           mode      = get_arith_mode(mode);

	ir_node   *value     = symbolic_pop(mode);
	ir_node   *index     = symbolic_pop(mode_int);
	ir_node   *base_addr = symbolic_pop(mode_reference);
	ir_node   *in[1]     = { index };
	ir_node   *addr      = new_Sel(new_NoMem(), base_addr, 1, in, entity);

	ir_node   *mem       = get_store();
	ir_node   *store     = new_Store(mem, addr, value, cons_none);
	ir_node   *new_mem   = new_Proj(store, mode_M, pn_Store_M);
	set_store(new_mem);
}

static void construct_new_array(ir_type *array_type, ir_node *count)
{
	ir_node *mem     = get_store();
	count            = new_Conv(count, mode_Iu);
	ir_node *alloc   = new_Alloc(mem, count, array_type, heap_alloc);
	ir_node *new_mem = new_Proj(alloc, mode_M, pn_Alloc_M);
	ir_node *res     = new_Proj(alloc, mode_reference, pn_Alloc_res);
	set_store(new_mem);
	symbolic_push(res);
}

static void construct_arraylength(void)
{
	ir_node *mem      = get_store();
	ir_node *arrayref = symbolic_pop(mode_reference);
	ir_node *symc     = create_symconst(builtin_arraylength);
	ir_node *in[]     = { arrayref };
	ir_type *type     = get_entity_type(builtin_arraylength);
	ir_node *call     = new_Call(mem, symc, sizeof(in)/sizeof(*in), in, type);
	ir_node *new_mem  = new_Proj(call, mode_M, pn_Call_M);
	set_store(new_mem);

	ir_node *ress = new_Proj(call, mode_T, pn_Call_T_result);
	ir_node *res  = new_Proj(ress, mode_int, 0);
	symbolic_push(res);
}

static void construct_conv(ir_mode *src, ir_mode *target)
{
	// FIXME: not sure if the Firm conv works according to the VM spec.
	ir_mode *arith_src = get_arith_mode(src);
	ir_mode *arith_target = get_arith_mode(target);


	ir_node *op = symbolic_pop(arith_src);

	unsigned src_bits = get_mode_size_bits(src);
	unsigned target_bits = get_mode_size_bits(target);

	if (target_bits < src_bits) {
		// FIXME: need to truncate!
	}

	ir_node *conv = new_Conv(op, arith_target);
	symbolic_push(conv);
}

static uint16_t get_16bit_arg(uint32_t *pos)
{
	uint32_t p     = *pos;
	uint8_t  b1    = code->code[p++];
	uint8_t  b2    = code->code[p++];
	uint16_t value = (b1 << 8) | b2;
	*pos = p;
	return value;
}

static uint32_t get_32bit_arg(uint32_t *pos)
{
	uint32_t p     = *pos;
	uint8_t  b1    = code->code[p++];
	uint8_t  b2    = code->code[p++];
	uint8_t  b3    = code->code[p++];
	uint8_t  b4    = code->code[p++];
	uint32_t value = (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
	*pos = p;
	return value;
}

static void code_to_firm(ir_entity *entity, const attribute_code_t *new_code)
{
	code = new_code;

	ir_type *method_type = get_entity_type(entity);

	max_locals = code->max_locals + get_method_n_params(method_type);
	ir_graph *irg = new_ir_graph(entity, max_locals + code->max_stack);
	current_ir_graph = irg;
	stack_pointer    = 0;

	/* arguments become local variables */
	ir_node *first_block = get_cur_block();
	set_cur_block(get_irg_start_block(irg));

	ir_node *args         = get_irg_args(irg);
	int      n_parameters = get_method_n_params(method_type);
	int      local_idx    = 0;
	for (int i = 0; i < n_parameters; ++i) {
		ir_type *type  = get_method_param_type(method_type, i);
		ir_mode *mode  = get_type_mode(type);
		ir_node *value = new_Proj(args, mode, i);
		value = get_arith_value(value);
		set_local(local_idx, value);
		local_idx++;
		if (needs_two_slots(mode)) local_idx++;
	}

	/* pass1: identify jump targets and create blocks for them */
	unsigned *targets = rbitset_malloc(code->code_length);
	basic_blocks = NEW_ARR_F(basic_block_t, 0);

	unsigned *ehs = rbitset_malloc(code->code_length);
	rbitset_clear_all(ehs, code->code_length);

	for (unsigned i = 0; i < new_code->n_exceptions; i++) {
		exception_t e = new_code->exceptions[i];

		if (! rbitset_is_set(targets, e.handler_pc)) {
			rbitset_set(targets, e.handler_pc);
			basic_block_t exception_handler;
			exception_handler.pc            = e.handler_pc;
			exception_handler.block         = new_immBlock();
			exception_handler.stack_pointer = -1;
			ARR_APP1(basic_block_t, basic_blocks, exception_handler);
		}

		rbitset_set(ehs, e.handler_pc);
	}

	basic_block_t start;
	start.pc            = 0;
	start.block         = first_block;
	start.stack_pointer = 0;
	ARR_APP1(basic_block_t, basic_blocks, start);
	rbitset_set(targets, 0);

	for (uint32_t i = 0; i < code->code_length; /* nothing */) {
		opcode_kind_t opcode = code->code[i++];
		switch (opcode) {
		case OPC_BIPUSH:
		case OPC_LDC:
		case OPC_ALOAD:
		case OPC_ILOAD:
		case OPC_LLOAD:
		case OPC_FLOAD:
		case OPC_DLOAD:
		case OPC_ISTORE:
		case OPC_LSTORE:
		case OPC_FSTORE:
		case OPC_DSTORE:
		case OPC_ASTORE:
		case OPC_NEWARRAY:
			i++;
			continue;

		case OPC_WIDE:
			opcode = code->code[i++];
			switch (opcode) {
			case OPC_ILOAD:
			case OPC_FLOAD:
			case OPC_ALOAD:
			case OPC_LLOAD:
			case OPC_DLOAD:
			case OPC_ISTORE:
			case OPC_FSTORE:
			case OPC_ASTORE:
			case OPC_LSTORE:
			case OPC_DSTORE:
			//case OPC_RET:
				i += 2;
				continue;
			case OPC_IINC:
				i += 4;
				continue;
			default:
				panic("unexpected wide prefix to opcode 0x%X", opcode);
			}

		case OPC_SIPUSH:
		case OPC_LDC_W:
		case OPC_LDC2_W:
		case OPC_IINC:
		case OPC_GETSTATIC:
		case OPC_PUTSTATIC:
		case OPC_GETFIELD:
		case OPC_PUTFIELD:
		case OPC_INVOKEVIRTUAL:
		case OPC_INVOKESTATIC:
		case OPC_INVOKESPECIAL:
		case OPC_NEW:
		case OPC_ANEWARRAY:
		case OPC_CHECKCAST:
		case OPC_INSTANCEOF:
			i+=2;
			continue;

		case OPC_MULTIANEWARRAY:
			i+=3;
			continue;

		case OPC_INVOKEINTERFACE:
			i+=4;
			continue;

		case OPC_GOTO:
		case OPC_IFNULL:
		case OPC_IFNONNULL:
		case OPC_ACMPEQ:
		case OPC_ACMPNE:
		case OPC_IFEQ:
		case OPC_IFNE:
		case OPC_IFLT:
		case OPC_IFGE:
		case OPC_IFGT:
		case OPC_IFLE:
		case OPC_ICMPEQ:
		case OPC_ICMPNE:
		case OPC_ICMPLT:
		case OPC_ICMPLE:
		case OPC_ICMPGT:
		case OPC_ICMPGE:
		case OPC_GOTO_W: {
			uint8_t  b1 = code->code[i++];
			uint8_t  b2 = code->code[i++];
			uint16_t index = (b1 << 8) | b2;
			if (opcode == OPC_GOTO_W) {
				index = (index << 8) | code->code[i++];
				index = (index << 8) | code->code[i++];
				index += i-5;
			} else {
				index += i-3;
			}

			assert(index < code->code_length);
			if (!rbitset_is_set(targets, index)) {
				rbitset_set(targets, index);

				basic_block_t target;
				target.pc            = index;
				target.block         = new_immBlock();
				target.stack_pointer = -1;
				ARR_APP1(basic_block_t, basic_blocks, target);
			}

			if (opcode != OPC_GOTO && opcode != OPC_GOTO_W) {
				assert(i < code->code_length);
				if (!rbitset_is_set(targets, i)) {
					rbitset_set(targets, i);

					basic_block_t target;
					target.pc            = i;
					target.block         = new_immBlock();
					target.stack_pointer = -1;
					ARR_APP1(basic_block_t, basic_blocks, target);
				}
			}

			continue;
		}

		case OPC_TABLESWITCH: {
			// i points to the instruction after the opcode. That instruction should be on a index that is a multiple of 4.
			uint32_t tswitch_index = i-1;
			uint8_t  padding = (4 - (i % 4)) % 4;
			switch (padding) {
			case 3: assert (code->code[i++] == 0); // FALL THROUGH
			case 2: assert (code->code[i++] == 0); // FALL THROUGH
			case 1: assert (code->code[i++] == 0); break;
			default: break;
			}

			int32_t  offset_default = get_32bit_arg(&i);
			uint32_t index_default  = ((int32_t)tswitch_index) + offset_default;

			assert (index_default < code->code_length);

			if (!rbitset_is_set(targets, index_default)) {
				rbitset_set(targets, index_default);

				basic_block_t target;
				target.pc            = index_default;
				target.block         = new_immBlock();
				target.stack_pointer = -1;
				ARR_APP1(basic_block_t, basic_blocks, target);
			}

			int32_t  low            = get_32bit_arg(&i);
			int32_t  high           = get_32bit_arg(&i);
			assert (low <= high);
			int32_t  n_entries      = high - low + 1;

			for (int32_t entry = 0; entry < n_entries; entry++) {
				int32_t offset = get_32bit_arg(&i);
				uint32_t index = ((int32_t)tswitch_index) + offset;
				assert(index < code->code_length);

				if (!rbitset_is_set(targets, index)) {
					rbitset_set(targets, index);

					basic_block_t target;
					target.pc            = index;
					target.block         = new_immBlock();
					target.stack_pointer = -1;
					ARR_APP1(basic_block_t, basic_blocks, target);
				}
			}

			continue;
		}

		case OPC_LOOKUPSWITCH: {
			// i points to the instruction after the opcode. That instruction should be on a index that is a multiple of 4.
			uint32_t lswitch_index = i-1;
			uint8_t  padding = (4 - (i % 4)) % 4;
			switch (padding) {
			case 3: assert (code->code[i++] == 0); // FALL THROUGH
			case 2: assert (code->code[i++] == 0); // FALL THROUGH
			case 1: assert (code->code[i++] == 0); break;
			default: break;
			}

			int32_t  offset_default = get_32bit_arg(&i);
			uint32_t index_default  = ((int32_t)lswitch_index) + offset_default;

			assert (index_default < code->code_length);

			if (!rbitset_is_set(targets, index_default)) {
				rbitset_set(targets, index_default);

				basic_block_t target;
				target.pc            = index_default;
				target.block         = new_immBlock();
				target.stack_pointer = -1;
				ARR_APP1(basic_block_t, basic_blocks, target);
			}

			int32_t n_pairs          = get_32bit_arg(&i);

			for (int32_t pair = 0; pair < n_pairs; pair++) {
				int32_t match  = get_32bit_arg(&i);
				int32_t offset = get_32bit_arg(&i);
				(void) match;

				uint32_t index = ((int32_t)lswitch_index) + offset;
				assert (index < code->code_length);
				if (!rbitset_is_set(targets, index)) {
					rbitset_set(targets, index);

					basic_block_t target;
					target.pc            = index;
					target.block         = new_immBlock();
					target.stack_pointer = -1;
					ARR_APP1(basic_block_t, basic_blocks, target);
				}
			}

			continue;
		}

		case OPC_IRETURN:
		case OPC_LRETURN:
		case OPC_FRETURN:
		case OPC_DRETURN:
		case OPC_ARETURN:
		case OPC_RETURN:
		case OPC_ATHROW: {
			if (i < code->code_length) {
				if (!rbitset_is_set(targets, i)) {
					rbitset_set(targets, i);

					basic_block_t target;
					target.pc            = i;
					target.block         = new_immBlock();
					target.stack_pointer = -1;
					ARR_APP1(basic_block_t, basic_blocks, target);
				}
			}

			continue;
		}

		case OPC_NOP:
		case OPC_ACONST_NULL:
		case OPC_ICONST_M1:
		case OPC_ICONST_0:
		case OPC_ICONST_1:
		case OPC_ICONST_2:
		case OPC_ICONST_3:
		case OPC_ICONST_4:
		case OPC_ICONST_5:
		case OPC_LCONST_0:
		case OPC_LCONST_1:
		case OPC_FCONST_0:
		case OPC_FCONST_1:
		case OPC_FCONST_2:
		case OPC_DCONST_0:
		case OPC_DCONST_1:
		case OPC_ILOAD_0:
		case OPC_ILOAD_1:
		case OPC_ILOAD_2:
		case OPC_ILOAD_3:
		case OPC_LLOAD_0:
		case OPC_LLOAD_1:
		case OPC_LLOAD_2:
		case OPC_LLOAD_3:
		case OPC_FLOAD_0:
		case OPC_FLOAD_1:
		case OPC_FLOAD_2:
		case OPC_FLOAD_3:
		case OPC_DLOAD_0:
		case OPC_DLOAD_1:
		case OPC_DLOAD_2:
		case OPC_DLOAD_3:
		case OPC_ALOAD_0:
		case OPC_ALOAD_1:
		case OPC_ALOAD_2:
		case OPC_ALOAD_3:
		case OPC_IALOAD:
		case OPC_LALOAD:
		case OPC_FALOAD:
		case OPC_DALOAD:
		case OPC_AALOAD:
		case OPC_BALOAD:
		case OPC_CALOAD:
		case OPC_SALOAD:
		case OPC_ISTORE_0:
		case OPC_ISTORE_1:
		case OPC_ISTORE_2:
		case OPC_ISTORE_3:
		case OPC_LSTORE_0:
		case OPC_LSTORE_1:
		case OPC_LSTORE_2:
		case OPC_LSTORE_3:
		case OPC_FSTORE_0:
		case OPC_FSTORE_1:
		case OPC_FSTORE_2:
		case OPC_FSTORE_3:
		case OPC_DSTORE_0:
		case OPC_DSTORE_1:
		case OPC_DSTORE_2:
		case OPC_DSTORE_3:
		case OPC_ASTORE_0:
		case OPC_ASTORE_1:
		case OPC_ASTORE_2:
		case OPC_ASTORE_3:
		case OPC_IASTORE:
		case OPC_LASTORE:
		case OPC_FASTORE:
		case OPC_DASTORE:
		case OPC_AASTORE:
		case OPC_BASTORE:
		case OPC_CASTORE:
		case OPC_SASTORE:
		case OPC_POP:
		case OPC_POP2:
		case OPC_DUP:
		case OPC_DUP_X1:
		case OPC_DUP_X2:
		case OPC_DUP2:
		case OPC_DUP2_X1:
		case OPC_DUP2_X2:
		case OPC_SWAP:
		case OPC_IADD:
		case OPC_LADD:
		case OPC_FADD:
		case OPC_DADD:
		case OPC_ISUB:
		case OPC_LSUB:
		case OPC_FSUB:
		case OPC_DSUB:
		case OPC_IMUL:
		case OPC_LMUL:
		case OPC_FMUL:
		case OPC_DMUL:
		case OPC_IDIV:
		case OPC_LDIV:
		case OPC_FDIV:
		case OPC_DDIV:
		case OPC_IREM:
		case OPC_LREM:
		case OPC_FREM:
		case OPC_DREM:
		case OPC_INEG:
		case OPC_LNEG:
		case OPC_FNEG:
		case OPC_DNEG:
		case OPC_ISHL:
		case OPC_LSHL:
		case OPC_ISHR:
		case OPC_LSHR:
		case OPC_IUSHR:
		case OPC_LUSHR:
		case OPC_IAND:
		case OPC_LAND:
		case OPC_IOR:
		case OPC_LOR:
		case OPC_IXOR:
		case OPC_LXOR:
		case OPC_I2L:
		case OPC_I2F:
		case OPC_I2D:
		case OPC_L2I:
		case OPC_L2F:
		case OPC_L2D:
		case OPC_F2I:
		case OPC_F2L:
		case OPC_F2D:
		case OPC_D2I:
		case OPC_D2L:
		case OPC_D2F:
		case OPC_I2B:
		case OPC_I2C:
		case OPC_I2S:
		case OPC_LCMP:
		case OPC_FCMPL:
		case OPC_FCMPG:
		case OPC_DCMPL:
		case OPC_DCMPG:
		case OPC_ARRAYLENGTH:
		case OPC_MONITORENTER:
		case OPC_MONITOREXIT:
			continue;
		}
		panic("unknown/unimplemented opcode 0x%X", opcode);
	}

	xfree(targets);

	n_basic_blocks = ARR_LEN(basic_blocks);
	qsort(basic_blocks, n_basic_blocks, sizeof(basic_blocks[0]),
	      compare_basic_blocks);

	basic_block_t end;
	end.pc            = code->code_length;
	end.block         = NULL;
	end.stack_pointer = -1;
	ARR_APP1(basic_block_t, basic_blocks, end);

	/* pass2: do a symbolic execution of the basic blocks and create firm node
	   while doing so */
	set_cur_block(NULL);
	basic_block_t *next_target = &basic_blocks[0];

	for (uint32_t i = 0; i < code->code_length; /* nothing */) {
		if (i == next_target->pc) {
			if (next_target->stack_pointer < 0) {
				next_target->stack_pointer = stack_pointer;
			} else {
				if (get_cur_block() != NULL
						&& next_target->stack_pointer != stack_pointer) {
					panic("stack pointer mismatch at PC %u\n", i);
				}
				stack_pointer = next_target->stack_pointer;
			}
			ir_node *next_block = next_target->block;
			if (get_cur_block() != NULL) {
				ir_node *jump = new_Jmp();
				add_immBlock_pred(next_block, jump);
			}
			set_cur_block(next_block);

			next_target++;
		} else {
			assert(i < next_target->pc);
		}

		// simulate that an exception object is pushed onto the stack
		// if we enter an execption handler
		if (rbitset_is_set(ehs, i))
			symbolic_push(new_Const_long(mode_reference, 0));

		opcode_kind_t opcode = code->code[i++];
		switch (opcode) {
		case OPC_NOP: continue;

		case OPC_ACONST_NULL: push_const(mode_reference, 0); continue;
		case OPC_ICONST_M1:   push_const(mode_int,      -1); continue;
		case OPC_ICONST_0:    push_const(mode_int,       0); continue;
		case OPC_ICONST_1:    push_const(mode_int,       1); continue;
		case OPC_ICONST_2:    push_const(mode_int,       2); continue;
		case OPC_ICONST_3:    push_const(mode_int,       3); continue;
		case OPC_ICONST_4:    push_const(mode_int,       4); continue;
		case OPC_ICONST_5:    push_const(mode_int,       5); continue;
		case OPC_LCONST_0:    push_const(mode_long,      0); continue;
		case OPC_LCONST_1:    push_const(mode_long,      1); continue;
		case OPC_FCONST_0:    push_const(mode_float,     0); continue;
		case OPC_FCONST_1:    push_const(mode_float,     1); continue;
		case OPC_FCONST_2:    push_const(mode_float,     2); continue;
		case OPC_DCONST_0:    push_const(mode_double,    0); continue;
		case OPC_DCONST_1:    push_const(mode_double,    1); continue;
		case OPC_BIPUSH:      push_const(mode_int, (int8_t) code->code[i++]); continue;
		case OPC_SIPUSH:      
			push_const(mode_int, (int16_t) get_16bit_arg(&i));
			continue;

		case OPC_LDC:       push_load_const(code->code[i++]);   continue;
		case OPC_LDC2_W:
		case OPC_LDC_W:     push_load_const(get_16bit_arg(&i)); continue;

		case OPC_ILOAD:   push_local(code->code[i++], mode_int);       continue;
		case OPC_LLOAD:   push_local(code->code[i++], mode_long);      continue;
		case OPC_FLOAD:   push_local(code->code[i++], mode_float);     continue;
		case OPC_DLOAD:   push_local(code->code[i++], mode_double);    continue;
		case OPC_ALOAD:   push_local(code->code[i++], mode_reference); continue;
		case OPC_ILOAD_0: push_local(0, mode_int);                     continue;
		case OPC_ILOAD_1: push_local(1, mode_int);                     continue;
		case OPC_ILOAD_2: push_local(2, mode_int);                     continue;
		case OPC_ILOAD_3: push_local(3, mode_int);                     continue;
		case OPC_LLOAD_0: push_local(0, mode_long);                    continue;
		case OPC_LLOAD_1: push_local(1, mode_long);                    continue;
		case OPC_LLOAD_2: push_local(2, mode_long);                    continue;
		case OPC_LLOAD_3: push_local(3, mode_long);                    continue;
		case OPC_FLOAD_0: push_local(0, mode_float);                   continue;
		case OPC_FLOAD_1: push_local(1, mode_float);                   continue;
		case OPC_FLOAD_2: push_local(2, mode_float);                   continue;
		case OPC_FLOAD_3: push_local(3, mode_float);                   continue;
		case OPC_DLOAD_0: push_local(0, mode_double);                  continue;
		case OPC_DLOAD_1: push_local(1, mode_double);                  continue;
		case OPC_DLOAD_2: push_local(2, mode_double);                  continue;
		case OPC_DLOAD_3: push_local(3, mode_double);                  continue;
		case OPC_ALOAD_0: push_local(0, mode_reference);               continue;
		case OPC_ALOAD_1: push_local(1, mode_reference);               continue;
		case OPC_ALOAD_2: push_local(2, mode_reference);               continue;
		case OPC_ALOAD_3: push_local(3, mode_reference);               continue;

		case OPC_IALOAD: construct_array_load(type_array_int);       continue;
		case OPC_LALOAD: construct_array_load(type_array_long);      continue;
		case OPC_FALOAD: construct_array_load(type_array_float);     continue;
		case OPC_DALOAD: construct_array_load(type_array_double);    continue;
		case OPC_AALOAD: construct_array_load(type_array_reference); continue;
		case OPC_BALOAD: construct_array_load(type_array_byte_boolean);continue;
		case OPC_CALOAD: construct_array_load(type_array_char);      continue;
		case OPC_SALOAD: construct_array_load(type_array_short);     continue;

		case OPC_ISTORE: pop_set_local(code->code[i++], mode_int);   continue;
		case OPC_LSTORE: pop_set_local(code->code[i++], mode_long);  continue;
		case OPC_FSTORE: pop_set_local(code->code[i++], mode_float); continue;
		case OPC_DSTORE: pop_set_local(code->code[i++], mode_double);continue;
		case OPC_ASTORE: pop_set_local(code->code[i++], mode_reference); continue;

		case OPC_ISTORE_0: pop_set_local(0, mode_int);       continue;
		case OPC_ISTORE_1: pop_set_local(1, mode_int);       continue;
		case OPC_ISTORE_2: pop_set_local(2, mode_int);       continue;
		case OPC_ISTORE_3: pop_set_local(3, mode_int);       continue;
		case OPC_LSTORE_0: pop_set_local(0, mode_long);      continue;
		case OPC_LSTORE_1: pop_set_local(1, mode_long);      continue;
		case OPC_LSTORE_2: pop_set_local(2, mode_long);      continue;
		case OPC_LSTORE_3: pop_set_local(3, mode_long);      continue;
		case OPC_FSTORE_0: pop_set_local(0, mode_float);     continue;
		case OPC_FSTORE_1: pop_set_local(1, mode_float);     continue;
		case OPC_FSTORE_2: pop_set_local(2, mode_float);     continue;
		case OPC_FSTORE_3: pop_set_local(3, mode_float);     continue;
		case OPC_DSTORE_0: pop_set_local(0, mode_double);    continue;
		case OPC_DSTORE_1: pop_set_local(1, mode_double);    continue;
		case OPC_DSTORE_2: pop_set_local(2, mode_double);    continue;
		case OPC_DSTORE_3: pop_set_local(3, mode_double);    continue;
		case OPC_ASTORE_0: pop_set_local(0, mode_reference); continue;
		case OPC_ASTORE_1: pop_set_local(1, mode_reference); continue;
		case OPC_ASTORE_2: pop_set_local(2, mode_reference); continue;
		case OPC_ASTORE_3: pop_set_local(3, mode_reference); continue;

		case OPC_IASTORE: construct_array_store(type_array_int);       continue;
		case OPC_LASTORE: construct_array_store(type_array_long);      continue;
		case OPC_FASTORE: construct_array_store(type_array_float);     continue;
		case OPC_DASTORE: construct_array_store(type_array_double);    continue;
		case OPC_AASTORE: construct_array_store(type_array_reference); continue;
		case OPC_BASTORE: construct_array_store(type_array_byte_boolean); continue;
		case OPC_CASTORE: construct_array_store(type_array_char);      continue;
		case OPC_SASTORE: construct_array_store(type_array_short);     continue;

		case OPC_POP:  --stack_pointer;                      continue;
		case OPC_POP2: stack_pointer -= 2;                   continue;

		case OPC_DUP:     construct_dup();                   continue;
		case OPC_DUP_X1:  construct_dup_x1();                continue;
		case OPC_DUP_X2:  construct_dup_x2();                continue;
		case OPC_DUP2:    construct_dup2();                  continue;
		case OPC_DUP2_X1: construct_dup2_x1();               continue;
		case OPC_DUP2_X2: construct_dup2_x2();               continue;
		case OPC_SWAP: {
			ir_node *top1 = symbolic_pop(NULL);
			ir_node *top2 = symbolic_pop(NULL);
			symbolic_push(top1);
			symbolic_push(top2);
			continue;
		}

		case OPC_IADD:  construct_arith(mode_int,    new_Add);        continue;
		case OPC_LADD:  construct_arith(mode_long,   new_Add);        continue;
		case OPC_FADD:  construct_arith(mode_float,  new_Add);        continue;
		case OPC_DADD:  construct_arith(mode_double, new_Add);        continue;
		case OPC_ISUB:  construct_arith(mode_int,    new_Sub);        continue;
		case OPC_LSUB:  construct_arith(mode_long,   new_Sub);        continue;
		case OPC_FSUB:  construct_arith(mode_float,  new_Sub);        continue;
		case OPC_DSUB:  construct_arith(mode_double, new_Sub);        continue;
		case OPC_IMUL:  construct_arith(mode_int,    new_Mul);        continue;
		case OPC_LMUL:  construct_arith(mode_long,   new_Mul);        continue;
		case OPC_FMUL:  construct_arith(mode_float,  new_Mul);        continue;
		case OPC_DMUL:  construct_arith(mode_double, new_Mul);        continue;
		case OPC_IDIV:  construct_arith(mode_int,    simple_new_Div); continue;
		case OPC_LDIV:  construct_arith(mode_long,   simple_new_Div); continue;
		case OPC_FDIV:  construct_arith(mode_float,  simple_new_Quot);continue;
		case OPC_DDIV:  construct_arith(mode_double, simple_new_Quot);continue;
		case OPC_IREM:  construct_arith(mode_int,    simple_new_Mod); continue;
		case OPC_LREM:  construct_arith(mode_long,   simple_new_Mod); continue;
		case OPC_FREM:  construct_arith(mode_float,  simple_new_Mod); continue;
		case OPC_DREM:  construct_arith(mode_double, simple_new_Mod); continue;

		case OPC_INEG:  construct_arith_unop(mode_int,    new_Minus); continue;
		case OPC_LNEG:  construct_arith_unop(mode_long,   new_Minus); continue;
		case OPC_FNEG:  construct_arith_unop(mode_float,  new_Minus); continue;
		case OPC_DNEG:  construct_arith_unop(mode_double, new_Minus); continue;

		case OPC_ISHL:  construct_shift_arith(mode_int,    new_Shl);  continue;
		case OPC_LSHL:  construct_shift_arith(mode_long,   new_Shl);  continue;
		case OPC_ISHR:  construct_shift_arith(mode_int,    new_Shrs); continue;
		case OPC_LSHR:  construct_shift_arith(mode_long,   new_Shrs); continue;
		case OPC_IUSHR: construct_shift_arith(mode_int,    new_Shr);  continue;
		case OPC_LUSHR: construct_shift_arith(mode_long,   new_Shr);  continue;
		case OPC_IAND:  construct_arith(mode_int,    new_And);        continue;
		case OPC_LAND:  construct_arith(mode_long,   new_And);        continue;
		case OPC_IOR:   construct_arith(mode_int,    new_Or);         continue;
		case OPC_LOR:   construct_arith(mode_long,   new_Or);         continue;
		case OPC_IXOR:  construct_arith(mode_int,    new_Eor);        continue;
		case OPC_LXOR:  construct_arith(mode_long,   new_Eor);        continue;

		case OPC_I2L:   construct_conv(mode_int, mode_long);          continue;
		case OPC_I2F:   construct_conv(mode_int, mode_float);         continue;
		case OPC_I2D:   construct_conv(mode_int, mode_double);        continue;
		case OPC_L2I:   construct_conv(mode_long, mode_int);          continue;
		case OPC_L2F:   construct_conv(mode_long, mode_float);        continue;
		case OPC_L2D:   construct_conv(mode_long, mode_double);       continue;
		case OPC_F2I:   construct_conv(mode_float, mode_int);         continue;
		case OPC_F2L:   construct_conv(mode_float, mode_long);        continue;
		case OPC_F2D:   construct_conv(mode_float, mode_double);      continue;
		case OPC_D2I:   construct_conv(mode_double, mode_int);        continue;
		case OPC_D2L:   construct_conv(mode_double, mode_long);       continue;
		case OPC_D2F:   construct_conv(mode_double, mode_float);      continue;
		case OPC_I2B:   construct_conv(mode_int, mode_byte);          continue;
		case OPC_I2C:   construct_conv(mode_int, mode_char);          continue;
		case OPC_I2S:   construct_conv(mode_int, mode_short);         continue;

		case OPC_LCMP:  {
			// FIXME: need real implementation
			ir_node *val2 = symbolic_pop(mode_long);
			ir_node *val1 = symbolic_pop(mode_long);
			(void) val1;
			(void) val2;
			symbolic_push(new_Const_long(mode_int, 0));

			continue;
		}

		case OPC_FCMPL:
		case OPC_FCMPG: {
			// FIXME: need real implementation
			ir_node *val2 = symbolic_pop(mode_float);
			ir_node *val1 = symbolic_pop(mode_float);
			(void) val1;
			(void) val2;
			symbolic_push(new_Const_long(mode_int, 0));
			continue;
		}

		case OPC_DCMPL:
		case OPC_DCMPG: {
			// FIXME: need real implementation
			ir_node *val2 = symbolic_pop(mode_double);
			ir_node *val1 = symbolic_pop(mode_double);
			(void) val1;
			(void) val2;
			symbolic_push(new_Const_long(mode_int, 0));
			continue;
		}

		case OPC_IINC: {
			uint8_t  index = code->code[i++];
			int8_t   cnst  = (int8_t) code->code[i++];
			ir_node *val   = get_local(index, mode_int);
			ir_node *cnode = new_Const_long(mode_int, cnst);
			ir_node *add   = new_Add(val, cnode, mode_int);
			set_local(index, add);
			continue;
		}

		case OPC_WIDE:
			opcode = code->code[i++];
			switch (opcode) {
			case OPC_ILOAD: push_local(get_16bit_arg(&i), mode_int);   continue;
			case OPC_LLOAD: push_local(get_16bit_arg(&i), mode_long);  continue;
			case OPC_FLOAD: push_local(get_16bit_arg(&i), mode_float); continue;
			case OPC_DLOAD:
				push_local(get_16bit_arg(&i), mode_double);
				continue;
			case OPC_ALOAD:
				push_local(get_16bit_arg(&i), mode_reference);
				continue;
			case OPC_ISTORE:
				pop_set_local(get_16bit_arg(&i), mode_int);
				continue;
			case OPC_LSTORE:
				pop_set_local(get_16bit_arg(&i), mode_long);
				continue;
			case OPC_FSTORE:
				pop_set_local(get_16bit_arg(&i), mode_float);
				continue;
			case OPC_DSTORE:
				pop_set_local(get_16bit_arg(&i), mode_double);
				continue;
			case OPC_ASTORE:
				pop_set_local(get_16bit_arg(&i), mode_reference);
				continue;
			/* case OPC_RET: */
			case OPC_IINC: {
				uint16_t index = get_16bit_arg(&i);
				int16_t  cnst  = (int16_t) get_16bit_arg(&i);
				ir_node *val   = get_local(index, mode_int);
				ir_node *cnode = new_Const_long(mode_int, cnst);
				ir_node *add   = new_Add(val, cnode, mode_int);
				set_local(index, add);
				continue;
			}
			default:
				panic("unexpected wide prefix to opcode 0x%X", opcode);
			}

		case OPC_IFEQ:
		case OPC_IFNE:
		case OPC_IFLT:
		case OPC_IFGE:
		case OPC_IFGT:
		case OPC_IFLE:
		case OPC_ICMPEQ:
		case OPC_ICMPNE:
		case OPC_ICMPLT:
		case OPC_ICMPLE:
		case OPC_ICMPGT:
		case OPC_ICMPGE: {
			uint16_t index = get_16bit_arg(&i);
			index += i-3;

			ir_node *val2;
			if (opcode >= OPC_IFEQ && opcode <= OPC_IFLE) {
				val2 = new_Const_long(mode_int, 0);
			} else {
				assert(opcode >= OPC_ICMPEQ && opcode <= OPC_ICMPLE);
				val2 = symbolic_pop(mode_int);
			}
			ir_node *val1 = symbolic_pop(mode_int);
			ir_node *cmp  = new_Cmp(val1, val2);

			long pnc;
			switch(opcode) {
			case OPC_IFEQ:
			case OPC_ICMPEQ: pnc = pn_Cmp_Eq; break;
			case OPC_IFNE:
			case OPC_ICMPNE: pnc = pn_Cmp_Lg; break;
			case OPC_IFLT:
			case OPC_ICMPLT: pnc = pn_Cmp_Lt; break;
			case OPC_IFLE:
			case OPC_ICMPLE: pnc = pn_Cmp_Le; break;
			case OPC_IFGT:
			case OPC_ICMPGT: pnc = pn_Cmp_Gt; break;
			case OPC_IFGE:
			case OPC_ICMPGE: pnc = pn_Cmp_Ge; break;
			default: abort();
			}

			ir_node *proj = new_Proj(cmp, mode_b, pnc);
			construct_cond(index, i, proj);
			continue;
		}

		case OPC_ACMPEQ:
		case OPC_ACMPNE:
		case OPC_IFNULL:
		case OPC_IFNONNULL: {
			uint16_t index = get_16bit_arg(&i);
			index += i-3;

			ir_node *val1 = symbolic_pop(mode_reference);
			ir_node *val2;
			if (opcode == OPC_IFNULL || opcode == OPC_IFNONNULL) {
				val2 = new_Const_long(mode_reference, 0);
			} else {
				val2 = symbolic_pop(mode_reference);
			}
			ir_node *cmp  = new_Cmp(val1, val2);

			long pnc;
			switch(opcode) {
			case OPC_ACMPEQ:
			case OPC_IFNULL:    pnc = pn_Cmp_Eq; break;
			case OPC_ACMPNE:
			case OPC_IFNONNULL: pnc = pn_Cmp_Lg; break;
			default: abort();
			}

			ir_node *proj = new_Proj(cmp, mode_b, pnc);
			construct_cond(index, i, proj);
			continue;
		}

		case OPC_GOTO:
		case OPC_GOTO_W: {
			uint8_t  b1    = code->code[i++];
			uint8_t  b2    = code->code[i++];
			uint16_t index = (b1 << 8) | b2;
			if (opcode == OPC_GOTO_W) {
				index = (index << 8) | code->code[i++];
				index = (index << 8) | code->code[i++];
				index += i-5;
			} else {
				index += i-3;
			}

			ir_node *jmp = new_Jmp();
			ir_node *target_block 
				= get_target_block_remember_stackpointer(index);
			add_immBlock_pred(target_block, jmp);

			keep_alive(target_block);
			set_cur_block(NULL);
			continue;
		}

		case OPC_IRETURN: construct_vreturn(method_type, mode_int);    continue;
		case OPC_LRETURN: construct_vreturn(method_type, mode_long);   continue;
		case OPC_FRETURN: construct_vreturn(method_type, mode_float);  continue;
		case OPC_DRETURN: construct_vreturn(method_type, mode_double); continue;
		case OPC_ARETURN: construct_vreturn(method_type, mode_reference); continue;
		case OPC_RETURN:  construct_vreturn(method_type, NULL);        continue;

		case OPC_GETSTATIC:
		case OPC_PUTSTATIC:
		case OPC_GETFIELD:
		case OPC_PUTFIELD: {
			uint16_t   index   = get_16bit_arg(&i);
			ir_entity *entity  = get_field_entity(index);
			ir_node   *value   = NULL;
			ir_node   *addr;

			ir_node *mem  = get_store();
			ir_type *type = get_entity_type(entity);
			ir_mode *mode = get_type_mode(type);
			ir_mode *arith_mode = get_arith_mode(mode);

			if (opcode == OPC_PUTSTATIC || opcode == OPC_PUTFIELD) {
				value = symbolic_pop(arith_mode);
			}
			
			if (opcode == OPC_GETSTATIC || opcode == OPC_PUTSTATIC) {
				addr = create_symconst(entity);
			} else {
				ir_node *object = symbolic_pop(mode_reference);
				addr            = new_simpleSel(new_NoMem(), object, entity);
			}

			if (opcode == OPC_GETSTATIC || opcode == OPC_GETFIELD) {
				ir_node *load    = new_Load(mem, addr, mode, cons_none);
				ir_node *new_mem = new_Proj(load, mode_M, pn_Load_M);
				ir_node *result  = new_Proj(load, mode, pn_Load_res);
				set_store(new_mem);
				result = get_arith_value(result);
				symbolic_push(result);
			} else {
				assert(opcode == OPC_PUTSTATIC || opcode == OPC_PUTFIELD);
				ir_node *store   = new_Store(mem, addr, value, cons_none);
				ir_node *new_mem = new_Proj(store, mode_M, pn_Store_M);
				set_store(new_mem);
			}
			continue;
		}

		case OPC_INVOKEVIRTUAL: {
			uint16_t   index  = get_16bit_arg(&i);
			ir_entity *entity = get_method_entity(index);
			ir_type   *type   = get_entity_type(entity);
			unsigned   n_args = get_method_n_params(type);
			ir_node   *args[n_args];

			for (int i = n_args-1; i >= 0; --i) {
				ir_type *arg_type = get_method_param_type(type, i);
				ir_mode *mode     = get_type_mode(arg_type);
				ir_mode *amode    = get_arith_mode(mode);
				ir_node *val      = symbolic_pop(amode);
				if (get_irn_mode(val) != mode)
					val = new_Conv(val, mode);
				args[i]           = val;
			}

			ir_node *mem      = get_store();
			ir_node *callee   = new_Sel(new_NoMem(), args[0], 0, NULL, entity);
			ir_node *call     = new_Call(mem, callee, n_args, args, type);
			ir_node *new_mem  = new_Proj(call, mode_M, pn_Call_M);
			set_store(new_mem);

			int n_res = get_method_n_ress(type);
			if (n_res > 0) {
				assert(n_res == 1);
				ir_type *res_type = get_method_res_type(type, 0);
				ir_mode *mode     = get_type_mode(res_type);
				ir_node *resproj  = new_Proj(call, mode_T, pn_Call_T_result);
				ir_node *res      = new_Proj(resproj, mode, 0);
				res = get_arith_value(res);
				symbolic_push(res);
			}
			continue;
		}

		case OPC_INVOKESTATIC: {
			uint16_t   index  = get_16bit_arg(&i);
			ir_entity *entity = get_method_entity(index);
			ir_node   *callee = create_symconst(entity);
			ir_type   *type   = get_entity_type(entity);
			unsigned   n_args = get_method_n_params(type);
			ir_node   *args[n_args];

			for (int i = n_args-1; i >= 0; --i) {
				ir_type *arg_type = get_method_param_type(type, i);
				ir_mode *mode     = get_type_mode(arg_type);
				ir_mode *amode    = get_arith_mode(mode);
				ir_node *val      = symbolic_pop(amode);
				if (get_irn_mode(val) != mode)
					val = new_Conv(val, mode);
				args[i]           = val;
			}
			ir_node *mem     = get_store();
			ir_node *call    = new_Call(mem, callee, n_args, args, type);
			ir_node *new_mem = new_Proj(call, mode_M, pn_Call_M);
			set_store(new_mem);

			int n_res = get_method_n_ress(type);
			if (n_res > 0) {
				assert(n_res == 1);
				ir_type *res_type = get_method_res_type(type, 0);
				ir_mode *mode     = get_type_mode(res_type);
				ir_node *resproj  = new_Proj(call, mode_T, pn_Call_T_result);
				ir_node *res      = new_Proj(resproj, mode, 0);
				res = get_arith_value(res);
				symbolic_push(res);
			}
			continue;
		}

		case OPC_INVOKESPECIAL: {
			uint16_t   index   = get_16bit_arg(&i);
			ir_entity *entity  = get_method_entity(index);
			ir_node   *callee  = create_symconst(entity);
			ir_type   *type   = get_entity_type(entity);
			unsigned   n_args = get_method_n_params(type);
			ir_node   *args[n_args];

			for (int i = n_args-1; i >= 0; --i) {
				ir_type *arg_type = get_method_param_type(type, i);
				ir_mode *mode     = get_type_mode(arg_type);
				ir_mode *amode    = get_arith_mode(mode);
				ir_node *val      = symbolic_pop(amode);
				if (get_irn_mode(val) != mode)
					val = new_Conv(val, mode);
				args[i]           = val;
			}
			ir_node   *mem     = get_store();
			ir_node   *call    = new_Call(mem, callee, n_args, args, type);
			ir_node   *new_mem = new_Proj(call, mode_M, pn_Call_M);
			set_store(new_mem);

			int n_res = get_method_n_ress(type);
			if (n_res > 0) {
				assert(n_res == 1);
				ir_type *res_type = get_method_res_type(type, 0);
				ir_mode *mode     = get_type_mode(res_type);
				ir_node *resproj  = new_Proj(call, mode_T, pn_Call_T_result);
				ir_node *res      = new_Proj(resproj, mode, 0);
				res = get_arith_value(res);
				symbolic_push(res);
			}
			continue;
		}

		case OPC_INVOKEINTERFACE: {
			uint16_t   index   = get_16bit_arg(&i);
			uint8_t    count   = code->code[i++];
			assert (code->code[i++] == 0);
			ir_entity *entity = get_interface_entity(index);

			ir_type   *type   = get_entity_type(entity);
			unsigned   n_args = get_method_n_params(type);
			assert (n_args == count);
			ir_node   *args[n_args];

			for (int i = n_args-1; i >= 0; --i) {
				ir_type *arg_type = get_method_param_type(type, i);
				ir_mode *mode     = get_type_mode(arg_type);
				ir_mode *amode    = get_arith_mode(mode);
				ir_node *val      = symbolic_pop(amode);
				if (get_irn_mode(val) != mode)
					val = new_Conv(val, mode);
				args[i]           = val;
			}
			ir_node *mem     = get_store();
			ir_node *callee  = new_Sel(new_NoMem(), args[0], 0, NULL, entity);
			ir_node *call    = new_Call(mem, callee, n_args, args, type);
			ir_node *new_mem = new_Proj(call, mode_M, pn_Call_M);
			set_store(new_mem);

			int n_res = get_method_n_ress(type);
			if (n_res > 0) {
				assert(n_res == 1);
				ir_type *res_type = get_method_res_type(type, 0);
				ir_mode *mode     = get_type_mode(res_type);
				ir_node *resproj  = new_Proj(call, mode_T, pn_Call_T_result);
				ir_node *res      = new_Proj(resproj, mode, 0);
				res = get_arith_value(res);
				symbolic_push(res);
			}
			continue;
		}

		case OPC_NEW: {
			uint16_t  index     = get_16bit_arg(&i);
			ir_type  *classtype = get_classref_type(index);
			ir_node  *mem       = get_store();
			ir_node  *count     = new_Const_long(mode_Iu, 1);
			ir_node  *alloc     = new_Alloc(mem, count, classtype, heap_alloc);
			ir_node  *new_mem   = new_Proj(alloc, mode_M, pn_Alloc_M);
			ir_node  *result    = new_Proj(alloc, mode_reference, pn_Alloc_res);
			set_store(new_mem);
			symbolic_push(result);
			continue;
		}
		case OPC_NEWARRAY: {
			uint8_t ti = code->code[i++];
			ir_type *type;
			switch (ti) {
			case 4:  type = type_array_byte_boolean; break;
			case 5:  type = type_array_char;         break;
			case 6:  type = type_array_float;        break;
			case 7:  type = type_array_double;       break;
			case 8:  type = type_array_byte_boolean; break;
			case 9:  type = type_array_short;        break;
			case 10: type = type_array_int;          break;
			case 11: type = type_array_long;         break;
			default: panic("invalid type %u for NEWARRAY opcode", ti);
			}
			ir_node *count = symbolic_pop(mode_int);
			construct_new_array(type, count);
			continue;
		}
		case OPC_ANEWARRAY: {
			uint16_t index        = get_16bit_arg(&i);
			ir_type *element_type = get_classref_type(index);
			ir_type *type         = new_type_array(1, element_type);
			ir_node *count        = symbolic_pop(mode_int);
			construct_new_array(type, count);
			continue;
		}
		case OPC_ARRAYLENGTH: {
			construct_arraylength();
			continue;
		}

		case OPC_CHECKCAST: {
			// FIXME: need real implementation.
			uint16_t index        = get_16bit_arg(&i);
			ir_node *addr         = symbolic_pop(mode_reference);
			symbolic_push(addr);
			(void) index;
			continue;
		}
		case OPC_INSTANCEOF: {
			// FIXME: need real implementation.
			uint16_t index        = get_16bit_arg(&i);
			ir_node *addr         = symbolic_pop(mode_reference);
			push_const(mode_int, 0);

			(void) index;
			(void) addr;
			continue;
		}
		case OPC_ATHROW: {
			// FIXME: need real implementation.
			ir_node *addr         = symbolic_pop(mode_reference);
			// FIXME: The reference popped here must be topstack when entering the exception handler.
			// Currently a null-reference is pushed onto the stack when entering an exception handler

			(void) addr;

			ir_node *ret;

			ir_type *type = get_entity_type(entity);
			int n_ress    = get_method_n_ress(type);
			if (n_ress > 0) {
				assert (n_ress == 1);
				ir_type *res = get_method_res_type(type, 0);
				ir_mode *res_mode = get_type_mode(res);
				ir_node *fake_res = new_Const_long(res_mode, 0);
				ir_node *fake_res_arr[] = {fake_res};
				ret = new_Return(get_store(), 1, fake_res_arr);
			} else {
				ret = new_Return(get_store(), 0, NULL);
			}

			ir_node *end_block = get_irg_end_block(current_ir_graph);
			add_immBlock_pred(end_block, ret);
			set_cur_block(NULL);
			continue;
		}
		case OPC_MONITORENTER:
		case OPC_MONITOREXIT: {
			// FIXME: need real implementation.
			ir_node *addr         = symbolic_pop(mode_reference);
			(void) addr;
			continue;
		}
		case OPC_MULTIANEWARRAY: {
			// FIXME: need real implementation.
			uint16_t index        = get_16bit_arg(&i);
			uint8_t  dims         = code->code[i++];
			(void) index;

			for (int ci = 0; ci < dims; ci++) {
				ir_node *unused = symbolic_pop(mode_int);
				(void) unused;
			}

			symbolic_push(new_Const_long(mode_reference, 0));
			continue;
		}

		case OPC_TABLESWITCH: {
			// FIXME: real implementation

			// i points to the instruction after the opcode. That instruction should be on a index that is a multiple of 4.
			uint32_t tswitch_index = i-1;
			uint8_t  padding = (4 - (i % 4)) % 4;
			switch (padding) {
			case 3: assert (code->code[i++] == 0); // FALL THROUGH
			case 2: assert (code->code[i++] == 0); // FALL THROUGH
			case 1: assert (code->code[i++] == 0); break;
			default: break;
			}

			int32_t  offset_default = get_32bit_arg(&i);
			uint32_t index_default  = ((int32_t)tswitch_index) + offset_default;
			assert (index_default < code->code_length);

			int32_t  low            = get_32bit_arg(&i);
			int32_t  high           = get_32bit_arg(&i);
			assert (low <= high);
			int32_t  n_entries      = high - low + 1;

			for (int32_t entry = 0; entry < n_entries; entry++) {
				int32_t offset = get_32bit_arg(&i);
				uint32_t index = ((int32_t)tswitch_index) + offset;
				assert(index < code->code_length);
			}

			ir_node *op = symbolic_pop(mode_int);
			(void) op;

			continue;
		}

		case OPC_LOOKUPSWITCH: {
			// FIXME: real implementation

			// i points to the instruction after the opcode. That instruction should be on a index that is a multiple of 4.
			uint32_t lswitch_index = i-1;
			uint8_t  padding = (4 - (i % 4)) % 4;
			switch (padding) {
			case 3: assert (code->code[i++] == 0); // FALL THROUGH
			case 2: assert (code->code[i++] == 0); // FALL THROUGH
			case 1: assert (code->code[i++] == 0); break;
			default: break;
			}

			int32_t  offset_default = get_32bit_arg(&i);
			uint32_t index_default  = ((int32_t)lswitch_index) + offset_default;

			assert (index_default < code->code_length);

			int32_t n_pairs          = get_32bit_arg(&i);

			ir_node *op = symbolic_pop(mode_int);

			for (int pair = 0; pair < n_pairs; pair++) {
				int32_t match  = get_32bit_arg(&i);
				int32_t offset = get_32bit_arg(&i);

				uint32_t index = ((int32_t)lswitch_index) + offset;
				assert (index < code->code_length);

				ir_node *const_match = new_Const_long(mode_int, match);
				ir_node *cmp         = new_Cmp(op, const_match);
				ir_node *proj_eq     = new_Proj(cmp, mode_b, pn_Cmp_Eq);

				ir_node *block_true  = get_target_block_remember_stackpointer(index);
				ir_node *block_false = new_immBlock();

				ir_node *cond        = new_Cond(proj_eq);
				ir_node *proj_true   = new_Proj(cond, mode_X, pn_Cond_true);
				add_immBlock_pred(block_true, proj_true);
				ir_node *proj_false  = new_Proj(cond, mode_X, pn_Cond_false);
				add_immBlock_pred(block_false, proj_false);
				mature_immBlock(block_false);

				set_cur_block(block_false);
			}

			ir_node *default_block = get_target_block_remember_stackpointer(index_default);
			ir_node *jmp           = new_Jmp();
			add_immBlock_pred(default_block, jmp);

			set_cur_block(NULL);

			continue;
		}
		}

		panic("unknown/unimplemented opcode 0x%X found\n", opcode);
	}

	xfree(ehs);

	for (size_t t = 0; t < n_basic_blocks; ++t) {
		basic_block_t *basic_block = &basic_blocks[t];
		mature_immBlock(basic_block->block);
	}
 	ir_node *end_block = get_irg_end_block(irg);
	mature_immBlock(end_block);

	DEL_ARR_F(basic_blocks);

	ir_type *frame_type = get_irg_frame_type(current_ir_graph);
	set_type_size_bytes(frame_type, 0);
	set_type_alignment_bytes(frame_type, 4);
	set_type_state(frame_type, layout_fixed);
}

static void create_method_entity(method_t *method, ir_type *owner)
{
	const char *name         = get_constant_string(method->name_index);
	ident      *id           = new_id_from_str(name);
	const char *descriptor   = get_constant_string(method->descriptor_index);
	ident      *descriptorid = new_id_from_str(descriptor);
	ir_type    *type         = method_descriptor_to_type(descriptor, owner,
	                                                     method->access_flags);
	ident      *mangled_id   = id_mangle_dot(id, descriptorid);
	ir_entity  *entity       = new_entity(owner, mangled_id, type);
	set_entity_link(entity, method);

	if (! (method->access_flags & ACCESS_FLAG_STATIC)) {
		assert(is_Class_type(owner));
		ir_type *superclass_type = owner;
		ir_entity *superclass_method = NULL;

		while(superclass_type != NULL && superclass_method == NULL) {
			int n_supertypes = get_class_n_supertypes(superclass_type);
			if (n_supertypes > 0) {
					assert (n_supertypes == 1);
					superclass_type = get_class_supertype(superclass_type, 0);
					ir_entity *member_in_superclass = get_class_member_by_name(superclass_type, mangled_id);
					if (member_in_superclass != NULL && is_method_entity(member_in_superclass)) {
						superclass_method = member_in_superclass;
					}
			} else {
				superclass_type = NULL;
			}
		}

		if (superclass_method != NULL) {
			add_entity_overwrites(entity, superclass_method);
		}
	}

	ident *ld_ident;
	if (method->access_flags & ACCESS_FLAG_STATIC) {
		set_entity_allocation(entity, allocation_static);
	}
	if (method->access_flags & ACCESS_FLAG_NATIVE) {
		set_entity_visibility(entity, ir_visibility_external);
	}
	if (strcmp(name, "main") == 0
	 && strcmp(descriptor, "([Ljava/lang/String;)V") == 0
	 && strcmp(main_class_name, get_class_name(owner)) == 0) {
		ld_ident = new_id_from_str("__bc2firm_main");
	} else {
		ld_ident = mangle_entity_name(entity, id);
	}
	set_entity_ld_ident(entity, ld_ident);
}

static void create_method_code(ir_entity *entity)
{
	assert(is_Method_type(get_entity_type(entity)));
#ifdef VERBOSE
	fprintf(stderr, "...%s\n", get_entity_name(entity));
#endif

	/* transform code to firm graph */
	const method_t *method = get_entity_link(entity);
	for (size_t a = 0; a < (size_t) method->n_attributes; ++a) {
		const attribute_t *attribute = method->attributes[a];
		if (attribute->kind != ATTRIBUTE_CODE)
			continue;
		code_to_firm(entity, &attribute->code);
	}
}

static ir_type *get_class_type(const char *name)
{
	ir_type *type = class_registry_get(name);
	if (get_type_link(type) != NULL)
		return type;

#ifdef VERBOSE
	fprintf(stderr, "==> reading class %s\n", name);
#endif

	class_t *cls = read_class(name);
	set_type_link(type, cls);

	class_t *old_class_file = class_file;
	class_file = cls;

	if (class_file->super_class != 0) {
		ir_type *supertype = get_classref_type(class_file->super_class);
		assert (supertype != type);
		add_class_supertype(type, supertype);
		ir_entity *superclass_vptr = get_class_member_by_name(supertype, vptr_ident);
		ir_entity *vptr = new_entity(type, vptr_ident, type_reference);
		add_entity_overwrites(vptr, superclass_vptr);
	} else {
		/* this should only happen for java.lang.Object */
		assert(strcmp(name, "java/lang/Object") == 0);
		new_entity(type, vptr_ident, type_reference);
	}

	for (size_t f = 0; f < (size_t) class_file->n_fields; ++f) {
		field_t *field = class_file->fields[f];
		create_field_entity(field, type);
	}
	for (size_t m = 0; m < (size_t) class_file->n_methods; ++m) {
		method_t *method = class_file->methods[m];
		create_method_entity(method, type);
	}

	assert(class_file == cls);
	class_file = old_class_file;

	/* put class in worklist so the method code is constructed later */
	pdeq_putr(worklist, type);

	return type;
}

static ir_type *get_classref_type(uint16_t index)
{
	constant_t *classref = get_constant(index);
	if (classref->kind != CONSTANT_CLASSREF) {
		panic("no classref at constant index %u", index);
	}
	ir_type *type = classref->base.link;
	if (type == NULL) {
		const char *classname
			= get_constant_string(classref->classref.name_index);
		if (classname[0] == '[') {
			type = complete_descriptor_to_type(classname);
		} else {
			type = get_class_type(classname);
			classref->base.link = type;
		}
	}

	return type;
}

static ir_type *construct_class_methods(ir_type *type)
{
#ifdef VERBOSE
	fprintf(stderr, "==> Construct methods of %s\n", get_class_name(type));
#endif

	class_t *old_class = class_file;
	class_file = get_type_link(type);

	int n_members = get_class_n_members(type);
	for (int m = 0; m < n_members; ++m) {
		ir_entity *member = get_class_member(type, m);
		if (!is_method_entity(member))
			continue;
		create_method_code(member);
	}

	assert(class_file == get_type_link(type));
	class_file = old_class;

	return type;
}

int main(int argc, char **argv)
{
	be_opt_register();
	firm_parameter_t params;
	memset(&params, 0, sizeof(params));
	const backend_params *be_params = be_get_backend_param();
	params.size = sizeof(params);

	ir_init(&params);
	init_types();
	class_registry_init();
	init_mangle();

	const char *classpath = "classes/";
	class_file_init(classpath);
	worklist = new_pdeq();

	if (argc < 2) {
		fprintf(stderr, "Syntax: %s class_file\n", argv[0]);
		return 0;
	}

	size_t arg_len        = strlen(argv[1]);
	main_class_name       = argv[1];
	main_class_name_short = argv[1] + arg_len - 1;
	while (main_class_name_short > main_class_name && *(main_class_name_short-1) != '/') main_class_name_short--;

	/* trigger loading of the class specified on commandline */
	get_class_type(argv[1]);

	while (!pdeq_empty(worklist)) {
		ir_type *classtype = pdeq_getl(worklist);

		const char *classname = get_class_name(classtype);
		if (strncmp("java/", classname, 5) != 0 && strncmp("javax/", classname, 6) != 0 && strncmp("gnu/", classname, 4) != 0)
			construct_class_methods(classtype);
	}

	irp_finalize_cons();
	//dump_all_ir_graphs("");

	lower_oo();

	int n_irgs = get_irp_n_irgs();
	for (int p = 0; p < n_irgs; ++p) {
		ir_graph *irg = get_irp_irg(p);

		fprintf(stderr, "\x0d===> Optimizing irg\t%d/%d                  ", p+1, n_irgs);

		optimize_reassociation(irg);
//		optimize_load_store(irg);
		optimize_graph_df(irg);
		place_code(irg);
		optimize_cf(irg);
		opt_if_conv(irg, be_params->if_conv_info);
		optimize_cf(irg);
		optimize_reassociation(irg);
		optimize_graph_df(irg);
		//opt_jumpthreading(irg);
//		optimize_load_store(irg);
		optimize_graph_df(irg);
		optimize_cf(irg);
	}

	fprintf(stderr, "\n");

	lwrdw_param_t param = {
			.enable        = 1,
			.little_endian = 1,
			.high_signed   = mode_long,
			.high_unsigned = mode_Lu, // Java does not have unsigned long
			.low_signed    = mode_Is,
			.low_unsigned  = mode_Iu,
			.create_intrinsic = be_params->arch_create_intrinsic_fkt,
			.ctx           = be_params->create_intrinsic_ctx
	};

	lower_dw_ops(&param);
	for (int p = 0; p < n_irgs; ++p) {
		ir_graph *irg = get_irp_irg(p);
		/* TODO: This shouldn't be needed but the backend sometimes finds
			     dead Phi nodes if we don't do this */
		edges_deactivate(irg);
		edges_activate(irg);
	}

	//dump_ir_prog_ext(dump_typegraph, "types.vcg");

	FILE *asm_out = fopen("bc2firm.S", "w");

	fprintf(stderr, "===> Running backend\n");

	be_parse_arg("omitfp");
	be_main(asm_out, "bytecode");

	fclose(asm_out);

	class_file_exit();
	deinit_mangle();

	char cmd_buffer[1024];
	sprintf(cmd_buffer, "gcc -g bc2firm.S librts.o -lgcj -lstdc++ -o %s", main_class_name_short);

	fprintf(stderr, "===> Assembling & linking (%s)\n", cmd_buffer);

	int retval = system(cmd_buffer);

	fprintf(stderr, "===> Done!\n");

	return retval;
}
