#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED

#include "class_file.h"
#include "opcodes.h"
#include "types.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <libgen.h>

#include "adt/error.h"
#include "adt/obst.h"
#include "adt/array.h"
#include "adt/raw_bitset.h"
#include "adt/pdeq.h"
#include "adt/cpmap.h"
#include "adt/hashptr.h"
#include "adt/xmalloc.h"

#include "class_registry.h"
#include "gcj_interface.h"
#include "oo_java.h"
#include "mangle.h"

#include <libfirm/firm.h>
#include <liboo/oo.h>
#include <liboo/dmemory.h>
#include <liboo/rtti.h>
#include <liboo/oo_nodes.h>
#include <liboo/eh.h>

//#define OOO
#ifdef OOO
#include <liboo/ooopt.h>
#endif

#define VERBOSE

extern FILE *fdopen (int __fd, __const char *__modes);
extern int mkstemp (char *__template);

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

static ident *vptr_ident;
static ident *subobject_ident;

ir_entity *vptr_entity; // there's exactly one vptr entity, member of java.lang.Object.

static void init_types(void)
{
	mode_byte
		= new_ir_mode("B", irms_int_number, 8, 1, irma_twos_complement, 8);
	type_byte = new_type_primitive(mode_byte);

	mode_char
		= new_ir_mode("C", irms_int_number, 16, 0, irma_twos_complement, 16);
	type_char = new_type_primitive(mode_char);

	mode_short
		= new_ir_mode("S", irms_int_number, 16, 1, irma_twos_complement, 16);
	type_short = new_type_primitive(mode_short);

	mode_int
		= new_ir_mode("I", irms_int_number, 32, 1, irma_twos_complement, 32);
	type_int = new_type_primitive(mode_int);

	mode_long
		= new_ir_mode("J", irms_int_number, 64, 1, irma_twos_complement, 64);
	type_long = new_type_primitive(mode_long);
//	set_type_alignment_bytes(type_long, 4); // Setting this creates an object layout equivalent to gcj. (but breaks other things)

	ir_mode *mode_boolean = mode_byte;
	type_boolean = new_type_primitive(mode_boolean);

	mode_float
		= new_ir_mode("F", irms_float_number, 32, 1, irma_ieee754, 0);
	type_float = new_type_primitive(mode_float);

	mode_double
		= new_ir_mode("D", irms_float_number, 64, 1, irma_ieee754, 0);
	type_double = new_type_primitive(mode_double);
//	set_type_alignment_bytes(type_double, 4); // Setting this creates an object layout equivalent to gcj. (but breaks other things)

	mode_reference = mode_P;

	type_array_byte_boolean = new_type_array(1, type_byte);
	set_type_state(type_array_byte_boolean, layout_fixed);
	set_array_lower_bound_int(type_array_byte_boolean, 0, 0);
	type_array_short        = new_type_array(1, type_short);
	set_type_state(type_array_short, layout_fixed);
	set_array_lower_bound_int(type_array_short, 0, 0);
	type_array_char         = new_type_array(1, type_char);
	set_type_state(type_array_char, layout_fixed);
	set_array_lower_bound_int(type_array_char, 0, 0);
	type_array_int          = new_type_array(1, type_int);
	set_type_state(type_array_int, layout_fixed);
	set_array_lower_bound_int(type_array_int, 0, 0);
	type_array_long         = new_type_array(1, type_long);
	set_type_state(type_array_long, layout_fixed);
	set_array_lower_bound_int(type_array_long, 0, 0);
	type_array_float        = new_type_array(1, type_float);
	set_type_state(type_array_float, layout_fixed);
	set_array_lower_bound_int(type_array_float, 0, 0);
	type_array_double       = new_type_array(1, type_double);
	set_type_state(type_array_double, layout_fixed);
	set_array_lower_bound_int(type_array_double, 0, 0);

	type_reference          = new_type_primitive(mode_reference);
	type_array_reference    = new_type_array(1, type_reference);
	set_array_lower_bound_int(type_array_reference, 0, 0);
	set_type_state(type_array_reference, layout_fixed);

	vptr_ident              = new_id_from_str("@vptr");
	subobject_ident         = new_id_from_str("@base");
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
	for (size_t i = 0; i < ARR_LEN(arguments); ++i) {
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
	ir_entity  *entity     = NULL;

	if (field->access_flags & ACCESS_FLAG_STATIC)
		entity = new_entity(get_glob_type(), id, type);
	else
		entity = new_entity(owner, id, type);

	const char *classname    = get_class_name(owner);
	ident      *ld_id        = mangle_member_name(classname, name, NULL);
	set_entity_ld_ident(entity, ld_id);

	oo_java_setup_field_info(entity, field);

	if (gcji_is_api_class(owner))
		set_entity_visibility(entity, ir_visibility_external);

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
		set_value(stack_pointer++, new_Bad(mode));

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
		set_value(code->max_stack + n+1, new_Bad(mode));
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

static ir_entity *find_entity(ir_type *classtype, const char *name, const char *desc)
{
	assert (is_Class_type(classtype));

	ir_entity *entity     = NULL;

	// the current class_file is managed like a stack. See: get_class_type(..)
	class_t *old_class    = class_file;
	class_t *linked_class = (class_t*) oo_get_type_link(classtype);
	class_file = linked_class;

	// 1. is the entity defined in this class?
	for (uint16_t i = 0; entity == NULL && i < class_file->n_methods; i++) {
		method_t *m = class_file->methods[i];
		const char *n = get_constant_string(m->name_index);
		const char *s = get_constant_string(m->descriptor_index);

		if (strcmp(name, n) == 0 && strcmp(desc, s) == 0) {
			entity = m->link;
		}
	}
	for (uint16_t i = 0; entity == NULL && i < class_file->n_fields; i++) {
		field_t *f = class_file->fields[i];
		const char *n = get_constant_string(f->name_index);
		const char *s = get_constant_string(f->descriptor_index);

		if (strcmp(name, n) == 0 && strcmp(desc, s) == 0) {
			entity = f->link;
		}
	}

	// 2. is the entity defined in the superclass?
	if (entity == NULL && class_file->super_class > 0) {
		ir_type *supertype = get_classref_type(class_file->super_class);
		assert (supertype);
		entity = find_entity(supertype, name, desc);
	}

	// 3. is the entity defined in an interface?
	if (entity == NULL && class_file->n_interfaces > 0) {
		for (uint16_t i = 0; i < class_file->n_interfaces && entity == NULL; i++) {
			uint16_t interface_ref = class_file->interfaces[i];
			ir_type *interface     = get_classref_type(interface_ref);
			assert (interface);
			entity = find_entity(interface, name, desc);
		}
	}

	assert (class_file == linked_class);
	class_file = old_class;

	return entity;
}

static ir_type *find_entity_defining_class(ir_type *classtype, const char *name, const char *desc)
{
	assert (is_Class_type(classtype));

	ir_type *defining_class = NULL;

	// the current class_file is managed like a stack. See: get_class_type(..)
	class_t *old_class    = class_file;
	class_t *linked_class = (class_t*) oo_get_type_link(classtype);
	class_file = linked_class;

	// 1. is the entity defined in this class?
	for (uint16_t i = 0; defining_class == NULL && i < class_file->n_methods; i++) {
		method_t *m = class_file->methods[i];
		const char *n = get_constant_string(m->name_index);
		const char *s = get_constant_string(m->descriptor_index);

		if (strcmp(name, n) == 0 && strcmp(desc, s) == 0) {
			defining_class = classtype;
		}
	}
	for (uint16_t i = 0; defining_class == NULL && i < class_file->n_fields; i++) {
		field_t *f = class_file->fields[i];
		const char *n = get_constant_string(f->name_index);
		const char *s = get_constant_string(f->descriptor_index);

		if (strcmp(name, n) == 0 && strcmp(desc, s) == 0) {
			defining_class = classtype;
		}
	}

	// 2. is the entity defined in the superclass?
	if (defining_class == NULL && class_file->super_class > 0) {
		ir_type *supertype = get_classref_type(class_file->super_class);
		assert (supertype);
		defining_class = find_entity_defining_class(supertype, name, desc);
	}

	// 3. is the entity defined in an interface?
	if (defining_class == NULL && class_file->n_interfaces > 0) {
		for (uint16_t i = 0; i < class_file->n_interfaces && defining_class == NULL; i++) {
			uint16_t interface_ref = class_file->interfaces[i];
			ir_type *interface     = get_classref_type(interface_ref);
			assert (interface);
			defining_class = find_entity_defining_class(interface, name, desc);
		}
	}

	assert (class_file == linked_class);
	class_file = old_class;

	return defining_class;
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
			// semantically, this is correct (array types support the methods of j.l.Object.
			// We might need real array types for type info stuff later.
			classtype = get_class_type("java/lang/Object");
		}

		const char *methodname
			= get_constant_string(name_and_type->name_and_type.name_index);
		const char *descriptor
			= get_constant_string(name_and_type->name_and_type.descriptor_index);

		entity = find_entity(classtype, methodname, descriptor);
		assert (entity && is_method_entity(entity));
		methodref->base.link = entity;
	}

	return entity;
}

static ir_type *get_method_defining_class(uint16_t index)
{
	constant_t *methodref = get_constant(index);
	if (methodref->kind != CONSTANT_METHODREF) {
		panic("get_method_entity index argument not a methodref");
	}

	const constant_t *name_and_type
	  = get_constant(methodref->methodref.name_and_type_index);
	if (name_and_type->kind != CONSTANT_NAMEANDTYPE) {
		panic("invalid name_and_type in method %u", index);
	}
	ir_type *classtype
	  = get_classref_type(methodref->methodref.class_index);

	if (! is_Class_type(classtype)) {
		// semantically, this is correct (array types support the methods of j.l.Object.
		// We might need real array types for type info stuff later.
		classtype = get_class_type("java/lang/Object");
	}

	const char *methodname
	  = get_constant_string(name_and_type->name_and_type.name_index);
	const char *descriptor
	  = get_constant_string(name_and_type->name_and_type.descriptor_index);

	return find_entity_defining_class(classtype, methodname, descriptor);
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
		const char *descriptor
			= get_constant_string(name_and_type->name_and_type.descriptor_index);

		entity = find_entity(classtype, methodname, descriptor);
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

		const char *fieldname
			= get_constant_string(name_and_type->name_and_type.name_index);
		const char *descriptor
			= get_constant_string(name_and_type->name_and_type.descriptor_index);

		entity = find_entity(classtype, fieldname, descriptor);
		assert (entity && !is_method_entity(entity));
		fieldref->base.link = entity;
	}

	return entity;
}

static ir_type *get_field_defining_class(uint16_t index)
{
	constant_t *fieldref = get_constant(index);
	if (fieldref->kind != CONSTANT_FIELDREF) {
		panic("get_field_entity index argumetn not a fieldref");
	}

	const constant_t *name_and_type
	  = get_constant(fieldref->fieldref.name_and_type_index);
	if (name_and_type->kind != CONSTANT_NAMEANDTYPE) {
		panic("invalid name_and_type in field %u", index);
	}
	ir_type *classtype
	  = get_classref_type(fieldref->fieldref.class_index);

	const char *fieldname
	  = get_constant_string(name_and_type->name_and_type.name_index);
	const char *descriptor
	  = get_constant_string(name_and_type->name_and_type.descriptor_index);

	return find_entity_defining_class(classtype, fieldname, descriptor);
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

static void push_const_tarval(ir_tarval *tv)
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

static void push_load_const(uint16_t index)
{
	const constant_t *constant = get_constant(index);
	switch (constant->kind) {
	case CONSTANT_INTEGER:
		push_const(mode_int, (int32_t) constant->integer.value);
		break;
	case CONSTANT_FLOAT: {
		float      val = *((float*) &constant->floatc.value);
		ir_tarval *tv  = new_tarval_from_double(val, mode_float);
		push_const_tarval(tv);
		break;
	}
	case CONSTANT_LONG: {
		char buf[128];
		uint64_t val = ((uint64_t)constant->longc.high_bytes << 32) | constant->longc.low_bytes;
		snprintf(buf, sizeof(buf), "%lld", (int64_t) val);
		ir_tarval *tv = new_tarval_from_str(buf, strlen(buf), mode_long);
		push_const_tarval(tv);
		break;
	}
	case CONSTANT_DOUBLE: {
		uint64_t val = ((uint64_t)constant->doublec.high_bytes << 32) | constant->doublec.low_bytes;
		assert(sizeof(uint64_t) == sizeof(double));
		double     dval = *((double*)&val);
		ir_tarval *tv   = new_tarval_from_double(dval, mode_double);
		push_const_tarval(tv);
		break;
	}
	case CONSTANT_STRING: {
		constant_t *utf8_const = get_constant(constant->string.string_index);

		ir_entity *string_const = gcji_emit_utf8_const(utf8_const, 0);
		ir_graph  *irg          = get_current_ir_graph();
		ir_node   *block        = get_r_cur_block(irg);
		ir_node   *mem          = get_store();
		ir_node   *res          = gcji_new_string(string_const, irg, block, &mem);
		set_store(mem);

		symbolic_push(res);
		break;
	}
	case CONSTANT_CLASSREF: {
		const char *classname = get_constant_string(constant->classref.name_index);
		ir_type *klass = get_class_type(classname);
		assert (klass);
		ir_entity *cdf = gcji_get_class_dollar_field(klass);
		assert (cdf);
		ir_node *cdf_symc = create_symconst(cdf);

		symbolic_push(cdf_symc);
		break;
	}
	default:
		panic("ldc without int, float, string or classref constant");
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

static void construct_xcmp(ir_mode *mode, ir_relation rel_1, ir_relation rel_2)
{
	ir_node *val2 = symbolic_pop(mode);
	ir_node *val1 = symbolic_pop(mode);

	ir_node *cmp_lt  = new_Cmp(val1, val2, rel_1);
	ir_node *cmp_gt  = new_Cmp(val1, val2, rel_2);
	ir_node *conv_lt = new_Conv(cmp_lt, mode_int);
	ir_node *conv_gt = new_Conv(cmp_gt, mode_int);
	ir_node *res     = new_Sub(conv_gt, conv_lt, mode_int);

	symbolic_push(res);
}

static void construct_array_load(ir_type *array_type)
{
	ir_node   *index     = symbolic_pop(mode_int);
	ir_node   *base_addr = symbolic_pop(mode_reference);
	           base_addr = new_Add(base_addr, new_Const_long(mode_reference, GCJI_DATA_OFFSET), mode_reference); // skip the j.l.Object subobject and the length field.

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
	ir_mode   *arith_mode= get_arith_mode(mode);

	ir_node   *op        = symbolic_pop(arith_mode); // need to pop the operand as 32 bit value, but...
	ir_node   *value     = new_Conv(op, mode);       // ... obey the real type when writing to memory.
	ir_node   *index     = symbolic_pop(mode_int);
	ir_node   *base_addr = symbolic_pop(mode_reference);
	           base_addr = new_Add(base_addr, new_Const_long(mode_reference, GCJI_DATA_OFFSET), mode_reference); // skip the j.l.Object subobject and the length field.

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

static void construct_conv(ir_mode *src, ir_mode *target)
{
	ir_mode *arith_src    = get_arith_mode(src);
	ir_mode *arith_target = get_arith_mode(target);

	ir_node *op = symbolic_pop(arith_src);

	ir_node *conv  = new_Conv(op, target);
	ir_node *conv2 = new_Conv(conv, arith_target);
	symbolic_push(conv2);
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

static void sort_exceptions(exception_t *excptns, size_t n)
{
	bool swapped;
	do {
		swapped = false;
		for (size_t i = 1; i < n; i++) {
			exception_t *ex1, *ex2;
			ex1 = &excptns[i-1];
			ex2 = &excptns[i];
			if (ex1->start_pc >  ex2->start_pc
			|| (ex1->start_pc == ex2->start_pc && ex1->end_pc < ex2->end_pc)) {
				exception_t tmp;
				tmp = *ex2;
				*ex2 = *ex1;
				*ex1 = tmp;
				swapped = true;
				continue;
			}
		}
	} while (swapped);
	// Note: must preserve the order of catch clauses for the same try.
	// Example: B extends A, try { ... } catch (B b) {} catch (A a) {}
}

#if 0
static void print_exception(exception_t *excptns, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		fprintf(stderr, "%d: (%d : %d) -> %d [%s]\n", i, excptns[i].start_pc, excptns[i].end_pc, excptns[i].handler_pc, get_class_name(get_classref_type(excptns[i].catch_type)));
	}
}
#endif

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

	unsigned *catch_begins = rbitset_malloc(code->code_length);
	rbitset_clear_all(catch_begins, code->code_length);

	unsigned *try_begins = rbitset_malloc(code->code_length);
	rbitset_clear_all(try_begins, code->code_length);

	unsigned *try_ends = rbitset_malloc(code->code_length);
	rbitset_clear_all(try_ends, code->code_length);

	size_t       n_excptns = new_code->n_exceptions;
	exception_t *excptns   = XMALLOCN(exception_t, n_excptns);
	memcpy(excptns, new_code->exceptions, n_excptns * sizeof(exception_t));
	sort_exceptions(excptns, n_excptns);

	for (unsigned i = 0; i < n_excptns; i++) {
		exception_t *e = &excptns[i];

		if (! rbitset_is_set(targets, e->handler_pc)) {
			rbitset_set(targets, e->handler_pc);
			basic_block_t exception_handler;
			exception_handler.pc            = e->handler_pc;
			exception_handler.block         = new_immBlock();
			exception_handler.stack_pointer = -1;
			ARR_APP1(basic_block_t, basic_blocks, exception_handler);
		}

		rbitset_set(catch_begins, e->handler_pc);
		rbitset_set(try_begins,   e->start_pc);
		rbitset_set(try_ends,     e->end_pc);
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

	eh_start_method();

	for (uint32_t i = 0; i < code->code_length; /* nothing */) {
		if (i == next_target->pc) {
			if (next_target->stack_pointer < 0) {
				if (get_cur_block() != NULL) {
					next_target->stack_pointer = stack_pointer;
				}
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

		// construct exception handling
		if (rbitset_is_set(catch_begins, i))
			symbolic_push(eh_get_exception_object());

		if (rbitset_is_set(try_begins, i)) {
			int last_endpc = -1;
			for (unsigned j = 0; j < n_excptns; j++) {
				exception_t *e = &excptns[j];
				if (e->start_pc == i) {
					if (e->end_pc != last_endpc) // exceptions are sorted
						eh_new_lpad();
					last_endpc = e->end_pc;

					ir_type *catch_type = get_classref_type(e->catch_type);
					assert (catch_type);
					ir_node *handler = get_target_block_remember_stackpointer(e->handler_pc);
					eh_add_handler(catch_type, handler);
				} else if (e->start_pc > i) {
					break;
				}
			}
		}

		if (rbitset_is_set(try_ends, i))
			eh_pop_lpad();

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
		case OPC_FDIV:  construct_arith(mode_float,  simple_new_Div); continue;
		case OPC_DDIV:  construct_arith(mode_double, simple_new_Div); continue;
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

		case OPC_LCMP:  construct_xcmp(mode_long,   ir_relation_less,           ir_relation_greater);           continue;
		case OPC_FCMPL: construct_xcmp(mode_float,  ir_relation_unordered_less, ir_relation_greater);           continue;
		case OPC_FCMPG: construct_xcmp(mode_float,  ir_relation_less,           ir_relation_unordered_greater); continue;
		case OPC_DCMPL: construct_xcmp(mode_double, ir_relation_unordered_less, ir_relation_greater);           continue;
		case OPC_DCMPG: construct_xcmp(mode_double, ir_relation_less,           ir_relation_unordered_greater); continue;

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


			ir_relation rel;
			switch(opcode) {
			case OPC_IFEQ:
			case OPC_ICMPEQ: rel = ir_relation_equal; break;
			case OPC_IFNE:
			case OPC_ICMPNE: rel = ir_relation_less_greater; break;
			case OPC_IFLT:
			case OPC_ICMPLT: rel = ir_relation_less; break;
			case OPC_IFLE:
			case OPC_ICMPLE: rel = ir_relation_less_equal; break;
			case OPC_IFGT:
			case OPC_ICMPGT: rel = ir_relation_greater; break;
			case OPC_IFGE:
			case OPC_ICMPGE: rel = ir_relation_greater_equal; break;
			default: abort();
			}

			ir_node *cmp  = new_Cmp(val1, val2, rel);
			construct_cond(index, i, cmp);
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

			ir_relation rel;
			switch(opcode) {
			case OPC_ACMPEQ:
			case OPC_IFNULL:    rel = ir_relation_equal; break;
			case OPC_ACMPNE:
			case OPC_IFNONNULL: rel = ir_relation_less_greater; break;
			default: abort();
			}

			ir_node *cmp  = new_Cmp(val1, val2, rel);
			construct_cond(index, i, cmp);
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

		case OPC_IRETURN: construct_vreturn(method_type, mode_int);       continue;
		case OPC_LRETURN: construct_vreturn(method_type, mode_long);      continue;
		case OPC_FRETURN: construct_vreturn(method_type, mode_float);     continue;
		case OPC_DRETURN: construct_vreturn(method_type, mode_double);    continue;
		case OPC_ARETURN: construct_vreturn(method_type, mode_reference); continue;
		case OPC_RETURN:  construct_vreturn(method_type, NULL);           continue;

		case OPC_GETSTATIC:
		case OPC_PUTSTATIC:
		case OPC_GETFIELD:
		case OPC_PUTFIELD: {
			uint16_t   index   = get_16bit_arg(&i);
			ir_entity *entity  = get_field_entity(index);
			ir_node   *value   = NULL;
			ir_node   *addr;

			ir_node *cur_mem = get_store();
			ir_type *type    = get_entity_type(entity);
			ir_mode *mode    = get_type_mode(type);
			ir_mode *arith_mode = get_arith_mode(mode);

			if (opcode == OPC_PUTSTATIC || opcode == OPC_PUTFIELD) {
				value = symbolic_pop(arith_mode);
			}

			if (opcode == OPC_GETSTATIC || opcode == OPC_PUTSTATIC) {
				ir_type  *owner  = get_field_defining_class(index);
				assert (owner);

				ir_graph *irg    = get_current_ir_graph();
				ir_node  *block  = get_cur_block();
				gcji_class_init(owner, irg, block, &cur_mem);
				addr = create_symconst(entity);
			} else {
				ir_node  *object = symbolic_pop(mode_reference);
				addr             = new_simpleSel(new_NoMem(), object, entity);
			}

			if (opcode == OPC_GETSTATIC || opcode == OPC_GETFIELD) {
				ir_node *load    = new_Load(cur_mem, addr, mode, cons_none);
				         cur_mem = new_Proj(load, mode_M, pn_Load_M);
				ir_node *result  = new_Proj(load, mode, pn_Load_res);
				set_store(cur_mem);
				result = get_arith_value(result);
				symbolic_push(result);
			} else {
				assert(opcode == OPC_PUTSTATIC || opcode == OPC_PUTFIELD);
				value = new_Conv(value, mode);
				ir_node *store   = new_Store(cur_mem, addr, value, cons_none);
				         cur_mem = new_Proj(store, mode_M, pn_Store_M);
				set_store(cur_mem);
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

			ir_node *callee   = new_Sel(new_NoMem(), args[0], 0, NULL, entity);
			ir_node *call     = eh_new_Call(callee, n_args, args, type);

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
			ir_type   *owner  = get_method_defining_class(index);
			assert (owner);
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
			ir_node *cur_mem = get_store();
			ir_node *block = get_r_cur_block(irg);
			gcji_class_init(owner, irg, block, &cur_mem);
			set_store(cur_mem);

			ir_node *call    = eh_new_Call(callee, n_args, args, type);

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
			ir_node   *call    = eh_new_Call(callee, n_args, args, type);

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
			(void) count;
			assert (code->code[i++] == 0);
			ir_entity *entity = get_interface_entity(index);

			ir_type   *type   = get_entity_type(entity);
			unsigned   n_args = get_method_n_params(type);
			//assert (n_args == count); // Wrong assertion. Count is the # of slots needed for the params, e.g. double contributes 2 to this value.
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
			ir_node *callee  = new_Sel(new_NoMem(), args[0], 0, NULL, entity);
			ir_node *call    = eh_new_Call(callee, n_args, args, type);

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
			set_array_lower_bound_int(type, 0, 0);
			ir_node *count        = symbolic_pop(mode_int);
			construct_new_array(type, count);
			continue;
		}
		case OPC_ARRAYLENGTH: {
			ir_node *cur_mem  = get_store();
			ir_node *arrayref = symbolic_pop(mode_reference);
			ir_node *arlen    = new_Arraylength(cur_mem, arrayref);
			ir_node *res      = new_Proj(arlen, mode_int, pn_Arraylength_res);
			cur_mem  = new_Proj(arlen, mode_M, pn_Arraylength_M);
			set_store(cur_mem);
			symbolic_push(res);
			continue;
		}

		case OPC_CHECKCAST: {
			// FIXME: as long as the exception handling does not work correctly, a failed CHECKCAST results in a scruffy abortion.
			uint16_t   index     = get_16bit_arg(&i);
			ir_node   *addr      = symbolic_pop(mode_reference);

			ir_type   *classtype = get_classref_type(index);
			assert(classtype);

			ir_node   *cur_mem   = get_store();
			gcji_checkcast(classtype, addr, irg, get_cur_block(), &cur_mem);
			set_store(cur_mem);

			symbolic_push(addr);
			continue;
		}
		case OPC_INSTANCEOF: {
			uint16_t index      = get_16bit_arg(&i);
			ir_node *addr       = symbolic_pop(mode_reference);

			ir_type *classtype  = get_classref_type(index);
			assert(classtype);

			ir_node *cur_mem    = get_store();
			ir_node *instanceof = new_InstanceOf(cur_mem, addr, classtype);
			ir_node *res_b      = new_Proj(instanceof, mode_b, pn_InstanceOf_res);
			         cur_mem    = new_Proj(instanceof, mode_M, pn_InstanceOf_M);
			set_store(cur_mem);

			ir_node *conv       = new_Conv(res_b, mode_Is);

			symbolic_push(conv);
			continue;
		}
		case OPC_ATHROW: {
			// FIXME: need real implementation.
			ir_node *addr         = symbolic_pop(mode_reference);
			// FIXME: The reference popped here must be topstack when entering the exception handler.
			// Currently a null-reference is pushed onto the stack when entering an exception handler

			ir_node *cur_mem = get_store();
			ir_node *raise = new_Raise(cur_mem, addr);
			ir_node *proj_X  = new_Proj(raise, mode_X, pn_Raise_X);
			ir_node *end_block = get_irg_end_block(current_ir_graph);
			add_immBlock_pred(end_block, proj_X);
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
			// XXX: create an Alloc node and lower it in liboo would be cleaner.
			uint16_t index        = get_16bit_arg(&i);
			uint8_t  dims         = code->code[i++];

			ir_node *block        = get_cur_block();
			ir_node *cur_mem      = get_store();

			ir_type *array_type   = get_classref_type(index);
			ir_node *array_class_ref = gcji_get_runtime_classinfo(array_type, irg, block, &cur_mem);
			ir_node **sizes = XMALLOCN(ir_node *, dims);

			for (int ci = dims-1; ci >= 0; ci--) {
				sizes[ci] = symbolic_pop(mode_int);
			}

			ir_node *marray = gcji_new_multiarray(array_class_ref, dims, sizes, irg, block, &cur_mem);
			set_store(cur_mem);

			xfree(sizes);

			symbolic_push(marray);
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

			int32_t  low            = get_32bit_arg(&i);
			int32_t  high           = get_32bit_arg(&i);
			assert (low <= high);

			ir_node *op             = symbolic_pop(mode_int);
			ir_node *switch_cond    = new_Cond(op);

			for (int32_t entry = low; entry <= high; entry++) {
				int32_t offset = get_32bit_arg(&i);
				uint32_t index = ((int32_t)tswitch_index) + offset;
				assert(index < code->code_length);

				ir_node *block = get_target_block_remember_stackpointer(index);
				ir_node *proj  = new_Proj(switch_cond, mode_X, entry);

				add_immBlock_pred(block, proj);
			}

			ir_node *def_proj       = new_Proj(switch_cond, mode_X, high+1); //FIXME: breaks when lo = Integer.MIN_VALUE && high = Integer.MAX_VALUE.
			ir_node *def_block      = get_target_block_remember_stackpointer(index_default);
			add_immBlock_pred(def_block, def_proj);
			set_Cond_default_proj(switch_cond, high+1);

			set_cur_block(NULL);
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

			int32_t n_pairs         = get_32bit_arg(&i);

			ir_node *op             = symbolic_pop(mode_int);
			ir_node *switch_cond    = new_Cond(op);

			int32_t  max_match      = 0;
			for (int pair = 0; pair < n_pairs; pair++) {
				int32_t match  = get_32bit_arg(&i);
				int32_t offset = get_32bit_arg(&i);

				max_match      = match; // pairs are in ascending order.

				uint32_t index = ((int32_t)lswitch_index) + offset;
				assert (index < code->code_length);

				ir_node *block = get_target_block_remember_stackpointer(index);
				ir_node *proj  = new_Proj(switch_cond, mode_X, match);

				add_immBlock_pred(block, proj);

			}

			ir_node *def_proj        = new_Proj(switch_cond, mode_X, max_match+1); //FIXME: breaks when match = Integer.MAX_VALUE and Integer.MIN_VALUE is used.
			ir_node *def_block       = get_target_block_remember_stackpointer(index_default);
			add_immBlock_pred(def_block, def_proj);
			set_Cond_default_proj(switch_cond, max_match+1);

			set_cur_block(NULL);

			continue;
		}
		}

		panic("unknown/unimplemented opcode 0x%X found\n", opcode);
	}

	xfree(catch_begins);
	xfree(try_begins);
	xfree(try_ends);
	xfree(excptns);

	eh_end_method();

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
	ir_entity  *entity       = NULL;

	if (method->access_flags & ACCESS_FLAG_STATIC)
		entity = new_entity(get_glob_type(), mangled_id, type);
	else
		entity = new_entity(owner, mangled_id, type);

	const char *classname    = get_class_name(owner);
	ident      *ld_id        = mangle_member_name(classname, name, descriptor);
	set_entity_ld_ident(entity, ld_id);

	oo_java_setup_method_info(entity, method, class_file);

	if (method->access_flags & ACCESS_FLAG_NATIVE || gcji_is_api_class(owner)) {
		set_entity_visibility(entity, ir_visibility_external);
	}
}

static void create_method_code(ir_entity *entity)
{
	assert(is_Method_type(get_entity_type(entity)));
#ifdef VERBOSE
	fprintf(stderr, "...%s\n", get_entity_name(entity));
#endif

	/* transform code to firm graph */
	const method_t *method = (method_t*) oo_get_entity_link(entity);
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
	if (oo_get_type_link(type) != NULL)
		return type;

#ifdef VERBOSE
	fprintf(stderr, "==> reading class %s\n", name);
#endif

	class_t *cls = read_class(name);

	class_t *old_class_file = class_file;
	class_file = cls;

	if (class_file->super_class != 0) {
		oo_java_setup_type_info(type, cls);
		ir_type *supertype = get_classref_type(class_file->super_class);
		assert (supertype != type);
		add_class_supertype(type, supertype);
		new_entity(type, subobject_ident, supertype);

	} else {
		/* this should only happen for java.lang.Object */
		assert(strcmp(name, "java/lang/Object") == 0);
		vptr_entity = new_entity(type, vptr_ident, type_reference);
		oo_java_setup_type_info(type, cls);
	}

	for (size_t f = 0; f < (size_t) class_file->n_fields; ++f) {
		field_t *field = class_file->fields[f];
		create_field_entity(field, type);
	}
	for (size_t m = 0; m < (size_t) class_file->n_methods; ++m) {
		method_t *method = class_file->methods[m];
		create_method_entity(method, type);
	}
	for (size_t i = 0; i < (size_t) class_file->n_interfaces; ++i) {
		ir_type *iface = get_classref_type(class_file->interfaces[i]);
		add_class_supertype(type, iface);
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

	class_t *old_class    = class_file;
	class_t *linked_class = (class_t*) oo_get_type_link(type);
	class_file = linked_class;

	for (uint16_t m = 0; m < class_file->n_methods; ++m) {
		ir_entity *member = class_file->methods[m]->link;
		create_method_code(member);
	}

	assert(class_file == linked_class);
	class_file = old_class;

	return type;
}


static void link_interface_method_recursive(ir_type *klass, ir_entity *interface_method)
{
	assert (is_Class_type(klass));
	assert (is_method_entity(interface_method));
	assert (oo_get_class_is_interface(get_entity_owner(interface_method)));

	ident *method_id = get_entity_ident(interface_method);
	ir_entity *m = get_class_member_by_name(klass, method_id);
	if (m) {
		if (get_entity_overwrites_index(m, interface_method) == INVALID_MEMBER_INDEX)
			add_entity_overwrites(m, interface_method);
		return;
	}

	if (! (oo_get_class_is_abstract(klass) || oo_get_class_is_interface(klass))) {
		/*
		 * this means there must be an implementation in a superclass
		 *
		 * Class C [foo()]      Interface I [foo()]
		 *     \                      /
		 *      \_________  ........./
		 *                \/
		 *            Class Sub []
		 *
		 */

		ir_entity *impl = NULL;
		ir_type   *cur_class = klass;

		// find the implementation
		while (! impl) {
			cur_class = oo_get_class_superclass(cur_class);
			assert (cur_class); // we assert that there will be superclasses as long as we haven't found an impl.
			impl = get_class_member_by_name(cur_class, method_id);
		}

		// copy the method into the interface's implementor
		ir_type   *impl_type = get_entity_type(impl);
		ir_entity *impl_copy = new_entity(klass, method_id, impl_type);
		set_entity_ld_ident(impl_copy, id_mangle3("inh__", get_entity_ld_ident(impl), ""));
		set_entity_visibility(impl_copy, ir_visibility_private);
		oo_copy_entity_info(impl, impl_copy);
		oo_set_method_is_inherited(impl_copy, true);

		ir_initializer_t *init = get_entity_initializer(impl); // XXX: required?
		set_entity_initializer(impl_copy, init);

		add_entity_overwrites(impl_copy, impl);
		add_entity_overwrites(impl_copy, interface_method);

		return;
	}

	size_t n_subclasses = get_class_n_subtypes(klass);
	for (size_t i = 0; i < n_subclasses; i++) {
		ir_type *subclass = get_class_subtype(klass, i);
		link_interface_method_recursive(subclass, interface_method);
	}
}

static void link_interface_methods(ir_type *klass, void *env)
{
	(void) env;

	assert (is_Class_type(klass));
	if (! oo_get_class_is_interface(klass))
		return;


	// don't need to iterate the class_t structure, as we are only interested in non-static methods
	size_t n_subclasses = get_class_n_subtypes(klass);
	if (n_subclasses == 0)
		return;

	size_t n_member = get_class_n_members(klass);
	for (size_t m = 0; m < n_member; m++) {
		ir_entity *member = get_class_member(klass, m);
		if (! is_method_entity(member) || oo_get_method_exclude_from_vtable(member))
			continue;

		for (size_t sc = 0; sc < n_subclasses; sc++) {
			ir_type *subclass = get_class_subtype(klass, sc);
			link_interface_method_recursive(subclass, member);
		}
	}
}

static void link_normal_method_recursive(ir_type *klass, ir_entity *superclass_method)
{
	assert (is_Class_type(klass));
	assert (is_method_entity(superclass_method));

	ident *method_id = get_entity_ident(superclass_method);
	ir_entity *m = get_class_member_by_name(klass, method_id);
	if (m) {
		if (get_entity_overwrites_index(m, superclass_method) == INVALID_MEMBER_INDEX)
			add_entity_overwrites(m, superclass_method);
		return;
	}

	size_t n_subclasses = get_class_n_subtypes(klass);
	for (size_t i = 0; i < n_subclasses; i++) {
		ir_type *subclass = get_class_subtype(klass, i);
		link_normal_method_recursive(subclass, superclass_method);
	}
}

static void link_normal_methods(ir_type *klass, void *env)
{
	(void) env;
	assert (is_Class_type(klass));

	if (oo_get_class_is_interface(klass))
		return;

	// don't need to iterate the class_t structure, as we are only interested in non-static methods
	size_t n_subclasses = get_class_n_subtypes(klass);
	if (n_subclasses == 0)
		return;

	size_t n_member = get_class_n_members(klass);
	for (size_t m = 0; m < n_member; m++) {
		ir_entity *member = get_class_member(klass, m);
		if (! is_method_entity(member) || oo_get_method_exclude_from_vtable(member))
			continue;

		for (size_t sc = 0; sc < n_subclasses; sc++) {
			ir_type *subclass = get_class_subtype(klass, sc);
			if (oo_get_class_is_interface(subclass))
				continue;

			link_normal_method_recursive(subclass, member);
		}
	}
}

static void link_methods(void)
{
	 // handle the interfaces first, because it might be required to copy method entities to implementing classes.
	 // (see example in link_interface_method_recursive)
	class_walk_super2sub(link_interface_methods, NULL, NULL);
	class_walk_super2sub(link_normal_methods, NULL, NULL);
}

static void layout_types(ir_type *type, void *env)
{
	(void) env;
	if (get_type_state(type) != layout_fixed)
		default_layout_compound_type(type);
}

/**
 * Return the path to ourself (if possible)
 */
static char *get_exe_path(void)
{
	/* in linux /proc/self/exe should be a symlink to us
	 * TODO: write windows/mac/whatever specific code
	 */
	char *buf = malloc(4096);
	ssize_t s = readlink("/proc/self/exe", buf, 2048);
	if (s < 0 || s == 2048)
		return NULL;
	return buf;
}

/**
 * assume the "rt" directory is in `readlink /proc/self/exe`/../rt
 */
static char *guess_rt_path(void)
{
	char *my_place = get_exe_path();
	char *my_dir   = dirname(my_place);
	char *buf      = malloc(4096);
	snprintf(buf, 4096, "%s/../rt", my_dir);
	free(my_place);
	return buf;
}

int main(int argc, char **argv)
{
	be_opt_register();

	ir_init(NULL);
	init_types();
	class_registry_init();
	oo_java_init();
	gcji_init();

	if (argc < 2 || argc > 8) {
		fprintf(stderr, "Syntax: %s [-cp <classpath>] [-bootclasspath <bootclasspath>] [-o <output file name>] class_file\n", argv[0]);
		return 0;
	}

	const char *classpath     = "./classes";
	const char *bootclasspath = NULL;
	const char *output_name   = NULL;

	int curarg = 1;
	while (curarg < argc) {
		if (strcmp("-cp", argv[curarg]) == 0 && (curarg+1) < argc) {
			classpath = argv[++curarg];
		} else if (strcmp("-bootclasspath", argv[curarg]) == 0 && (curarg+1) < argc) {
			bootclasspath = argv[++curarg];
		} else if (strcmp("-o", argv[curarg]) == 0 && (curarg+1) < argc) {
					output_name = argv[++curarg];
		} else {
			main_class_name = argv[curarg];
		}
		curarg++;
	}

	assert (main_class_name);
	size_t arg_len        = strlen(main_class_name);
	main_class_name_short = main_class_name + arg_len - 1;
	while (main_class_name_short > main_class_name && *(main_class_name_short-1) != '/') main_class_name_short--;

	if (bootclasspath != NULL) {
		class_file_init(classpath, bootclasspath);
	} else {
		char *guess = guess_rt_path();
		class_file_init(classpath, guess);
		free(guess);
	}

	if (! output_name)
		output_name = main_class_name_short;

	worklist = new_pdeq();

	/* read java.lang.Object first (makes vptr entity available) */
	get_class_type("java/lang/Object");

	/* trigger loading of the class specified on commandline */
	ir_type *main_class_type = get_class_type(main_class_name);
	ir_entity *main_cdf = gcji_get_class_dollar_field(main_class_type);

	while (!pdeq_empty(worklist)) {
		ir_type *classtype = pdeq_getl(worklist);

		if (! gcji_is_api_class(classtype))
			construct_class_methods(classtype);
	}

	irp_finalize_cons();
	//dump_all_ir_graphs("");

	link_methods();

	int n_irgs = get_irp_n_irgs();

#ifdef OOO
	for (int p = 0; p < n_irgs; ++p) {
		ir_graph *irg = get_irp_irg(p);
		oo_devirtualize_local(irg);
	}
#endif

	oo_lower();
	class_walk_super2sub(layout_types, NULL, NULL);
	lower_highlevel(0);

	for (int p = 0; p < n_irgs; ++p) {
		ir_graph *irg = get_irp_irg(p);

		fprintf(stderr, "\x0d===> Optimizing irg\t%d/%d                  ", p+1, n_irgs);
		// XXX: this is a mess.
//		optimize_reassociation(irg);
//		optimize_load_store(irg);
//		optimize_graph_df(irg);
		place_code(irg);
		optimize_cf(irg);
//		opt_if_conv(irg);
//		optimize_cf(irg);
//		optimize_reassociation(irg);
//		optimize_graph_df(irg);
//		opt_jumpthreading(irg);
//		conv_opt(irg);
		dead_node_elimination(irg);
//		fixpoint_vrp(irg);
//		optimize_load_store(irg);
//		optimize_graph_df(irg);
//		optimize_cf(irg);
	}

	fprintf(stderr, "\n");

	be_lower_for_target();

	for (int p = 0; p < n_irgs; ++p) {
		ir_graph *irg = get_irp_irg(p);
		/* TODO: This shouldn't be needed but the backend sometimes finds
			     dead Phi nodes if we don't do this */
		edges_deactivate(irg);
	}

	char asm_file[] = "bc2firm_asm_XXXXXX";
	int asm_fd = mkstemp(asm_file);
	FILE *asm_out = fdopen(asm_fd, "w");

	fprintf(stderr, "===> Running backend\n");

	//be_parse_arg("omitfp"); // stack unwinding becomes easier with the frame pointer.
	be_main(asm_out, "bytecode");

	fclose(asm_out);

	// we had to get the class$ above, because calling gcji_get_class_dollar_field after the class has been lowered (e.g. now) would create a new entity.
	assert (main_cdf);
	const char *main_cdf_ldident = get_entity_ld_name(main_cdf);

	char startup_file[] = "bc2firm_startup_XXXXXX";
	int startup_fd = mkstemp(startup_file);
	FILE *startup_out = fdopen(startup_fd, "w");
	fprintf(startup_out, "extern void JvRunMain(void* klass, int argc, const char **argv);\n");
	fprintf(startup_out, "extern void *%s;\n", main_cdf_ldident);
	fprintf(startup_out, "int main(int argc, const char **argv) { JvRunMain(&%s, argc, argv); return 0; }\n", main_cdf_ldident);
	fclose(startup_out);

	class_file_exit();
	gcji_deinit();
	oo_java_deinit();

	char cmd_buffer[1024];
	sprintf(cmd_buffer, "gcc -g -x assembler %s -x c %s -x none -lgcj -lstdc++ -L. -Wl,-R. -loo_rt -o %s", asm_file, startup_file, output_name);

	fprintf(stderr, "===> Assembling & linking (%s)\n", cmd_buffer);

	int retval = system(cmd_buffer);

	fprintf(stderr, "===> Done!\n");

	return retval;
}
