#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED

#include "class_file.h"
#include "opcodes.h"
#include "types.h"

#include <stdint.h>
#include <inttypes.h>
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
#include "driver/firm_opt.h"

#include "class_registry.h"
#include "gcj_interface.h"
#include "mangle.h"

#include <libfirm/be.h>
#include <libfirm/firm.h>
#include <liboo/oo.h>
#include <liboo/dmemory.h>
#include <liboo/rtti.h>
#include <liboo/nodes.h>
#include <liboo/eh.h>

//#define EXCEPTIONS

#ifndef CLASSPATH_GCJ
#define CLASSPATH_GCJ "build/gcj"
#endif
#ifndef CLASSPATH_SIMPLERT
#define CLASSPATH_SIMPLERT "build/simplert"
#endif

extern FILE *fdopen (int __fd, __const char *__modes);
extern int mkstemp (char *__template);

static pdeq    *worklist;
static class_t *class_file;
static const char *main_class_name;
static const char *main_class_name_short;
static bool     verbose;

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

/**
 * Gets firm type for the class with given name. Note that calling this function
 * will not ensure that all methods, fields, vtable and rtti is already
 * constructed, you must call finalize_class_type() if you need any of them.
 */
static ir_type *get_class_type(const char *name);
static void finalize_type(ir_type *type);
static void finalize_class_type(ir_type *type);
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

static ir_entity *abort_entity;
static ir_mode   *mode_float_arithmetic;

/* Get arithmetic mode for a mode. */
static ir_mode *get_ir_mode_arithmetic(ir_mode *mode)
{
	if (mode_is_float(mode) && mode_float_arithmetic != NULL) {
		return mode_float_arithmetic;
	}

	return mode;
}

static ir_node *get_value_as(ir_node *node, ir_mode *mode)
{
	if (get_irn_mode(node) == mode)
		return node;

	return new_Conv(node, mode);
}

static void init_types(void)
{
	mode_byte
		= new_int_mode("B", irma_twos_complement, 8, 1, 8);
	type_byte = new_type_primitive(mode_byte);

	mode_char
		= new_int_mode("C", irma_twos_complement, 16, 0, 16);
	type_char = new_type_primitive(mode_char);

	mode_short
		= new_int_mode("S", irma_twos_complement, 16, 1, 16);
	type_short = new_type_primitive(mode_short);

	mode_int
		= new_int_mode("I", irma_twos_complement, 32, 1, 32);
	type_int = new_type_primitive(mode_int);

	mode_long
		= new_int_mode("J", irma_twos_complement, 64, 1, 64);
	type_long = new_type_primitive(mode_long);
	set_type_alignment_bytes(type_long, 4);

	ir_mode *mode_boolean = mode_byte;
	type_boolean = new_type_primitive(mode_boolean);

	/* Note: ir_overflow_min_max is incompatible with the x86 backend, we should
	 * support both style and select whatever backend_params.float_int_overflow
	 * reports. This however needs some fixup code to be generated here. */
	mode_float
		= new_float_mode("F", irma_ieee754, 8, 23, ir_overflow_min_max);
	type_float = new_type_primitive(mode_float);

	mode_double
		= new_float_mode("D", irma_ieee754, 11, 52, ir_overflow_min_max);
	type_double = new_type_primitive(mode_double);
	set_type_alignment_bytes(type_double, 4);

	mode_reference = mode_P;

	const backend_params *params = be_get_backend_param();
	mode_float_arithmetic = params->mode_float_arithmetic;

	type_array_byte_boolean = new_type_array(type_byte);
	set_type_state(type_array_byte_boolean, layout_fixed);
	type_array_short        = new_type_array(type_short);
	set_type_state(type_array_short, layout_fixed);
	type_array_char         = new_type_array(type_char);
	set_type_state(type_array_char, layout_fixed);
	type_array_int          = new_type_array(type_int);
	set_type_state(type_array_int, layout_fixed);
	type_array_long         = new_type_array(type_long);
	set_type_state(type_array_long, layout_fixed);
	type_array_float        = new_type_array(type_float);
	set_type_state(type_array_float, layout_fixed);
	type_array_double       = new_type_array(type_double);
	set_type_state(type_array_double, layout_fixed);

	type_reference          = new_type_primitive(mode_reference);
	set_type_alignment_bytes(type_reference, 4);
	type_array_reference    = new_type_array(type_reference);
	set_type_state(type_array_reference, layout_fixed);

	ident *abort_id = new_id_from_str("abort");
	ir_type *abort_type = new_type_method(0, 0);
	add_method_additional_properties(abort_type, mtp_property_noreturn);
	abort_entity = create_compilerlib_entity(abort_id, abort_type);
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

	if (field->access_flags & ACCESS_FLAG_STATIC) {
		entity = new_entity(get_glob_type(), id, type);
		set_entity_initializer(entity, get_initializer_null());
	} else
		entity = new_entity(owner, id, type);

	const char *classname    = get_class_name(owner);
	ident      *ld_id        = mangle_member_name(classname, name, NULL);
	set_entity_ld_ident(entity, ld_id);

	oo_set_entity_link(entity, field);
	field->link = entity;

	if (oo_get_class_is_extern(owner))
		set_entity_visibility(entity, ir_visibility_external);
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
	assert(mode == NULL || (mode == get_arith_mode(mode)));

	/* double and long need 2 stackslots */
	if (needs_two_slots(mode))
		set_value(stack_pointer++, new_Bad(mode));

	set_value(stack_pointer++, node);
}

static ir_node *symbolic_pop(ir_mode *mode)
{
	assert(mode == NULL || (mode == get_arith_mode(mode)));

	if (stack_pointer == 0)
		panic("code produces stack underflow");

	if (mode == NULL) {
		mode = ir_guess_mode(stack_pointer-1);
		assert(mode != NULL);
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
	assert(n < max_locals);
	set_value(code->max_stack + n, node);
	ir_mode *mode = get_irn_mode(node);
	assert(mode == NULL || mode == get_arith_mode(mode));

	if (needs_two_slots(mode)) {
		assert(n+1 < max_locals);
		set_value(code->max_stack + n+1, new_Bad(mode));
	}
}

static ir_node *get_local(uint16_t n, ir_mode *mode)
{
	assert(n < max_locals);
	assert(mode == NULL || mode == get_arith_mode(mode));
	return get_value(code->max_stack + n, mode);
}

static ir_entity *find_entity(ir_type *classtype, const char *name,
                              const char *desc)
{
	assert(is_Class_type(classtype));

	// the current class_file is managed like a stack. See: get_class_type(..)
	class_t *old_class    = class_file;
	class_t *linked_class = (class_t*) oo_get_type_link(classtype);
	class_file = linked_class;

	// 1. is the entity defined in this class?
	ir_entity *entity = NULL;
	for (uint16_t i = 0; i < class_file->n_methods; i++) {
		method_t *m = class_file->methods[i];
		const char *n = get_constant_string(m->name_index);
		const char *s = get_constant_string(m->descriptor_index);

		if (strcmp(name, n) == 0 && strcmp(desc, s) == 0) {
			entity = m->link;
			break;
		}
	}
	if (entity == NULL) {
		for (uint16_t i = 0; i < class_file->n_fields; i++) {
			field_t *f = class_file->fields[i];
			const char *n = get_constant_string(f->name_index);
			const char *s = get_constant_string(f->descriptor_index);

			if (strcmp(name, n) == 0 && strcmp(desc, s) == 0) {
				entity = f->link;
				break;
			}
		}
	}

	// 2. is the entity defined in the superclass?
	if (entity == NULL && class_file->super_class > 0) {
		ir_type *supertype = get_classref_type(class_file->super_class);
		assert(supertype);
		entity = find_entity(supertype, name, desc);
	}

	// 3. is the entity defined in an interface?
	if (entity == NULL && class_file->n_interfaces > 0) {
		for (uint16_t i = 0; i < class_file->n_interfaces; i++) {
			uint16_t interface_ref = class_file->interfaces[i];
			ir_type *interface     = get_classref_type(interface_ref);
			assert(interface);
			entity = find_entity(interface, name, desc);
			if (entity != NULL)
				break;
		}
	}

	assert(class_file == linked_class);
	class_file = old_class;

	return entity;
}

static ir_type *find_entity_defining_class(ir_type *classtype, const char *name,
                                           const char *desc)
{
	assert(is_Class_type(classtype));

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
		assert(supertype);
		defining_class = find_entity_defining_class(supertype, name, desc);
	}

	// 3. is the entity defined in an interface?
	if (defining_class == NULL && class_file->n_interfaces > 0) {
		for (uint16_t i = 0; i < class_file->n_interfaces && defining_class == NULL; i++) {
			uint16_t interface_ref = class_file->interfaces[i];
			ir_type *interface     = get_classref_type(interface_ref);
			assert(interface);
			defining_class = find_entity_defining_class(interface, name, desc);
		}
	}

	assert(class_file == linked_class);
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
		finalize_class_type(classtype);

		if (!is_Class_type(classtype)) {
			// semantically, this is correct (array types support the methods
			// of java.lang.Object.
			// We might need real array types for type info stuff later.
			classtype = get_class_type("java/lang/Object");
		}

		const char *methodname
			= get_constant_string(name_and_type->name_and_type.name_index);
		const char *descriptor
			= get_constant_string(name_and_type->name_and_type.descriptor_index);

		entity = find_entity(classtype, methodname, descriptor);
		if (entity == NULL)
			panic("Couldn't find method %s.%s (%s)", get_class_name(classtype),
			      methodname, descriptor);

		assert(entity && is_method_entity(entity));
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

	if (!is_Class_type(classtype)) {
		// semantically, this is correct (array types support the methods of
		// java.lang.Object.
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
		finalize_class_type(classtype);

		const char *methodname
			= get_constant_string(name_and_type->name_and_type.name_index);
		const char *descriptor
			= get_constant_string(name_and_type->name_and_type.descriptor_index);

		entity = find_entity(classtype, methodname, descriptor);
		assert(entity && is_method_entity(entity));
		interfacemethodref->base.link = entity;
	}

	return entity;
}

static ir_entity *get_field_entity(uint16_t index)
{
	constant_t *fieldref = get_constant(index);
	if (fieldref->kind != CONSTANT_FIELDREF) {
		panic("get_field_entity index argument not a fieldref");
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
		finalize_class_type(classtype);

		const char *fieldname
			= get_constant_string(name_and_type->name_and_type.name_index);
		const char *descriptor
			= get_constant_string(name_and_type->name_and_type.descriptor_index);

		entity = find_entity(classtype, fieldname, descriptor);
		assert(entity && !is_method_entity(entity));
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
	finalize_class_type(classtype);

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
	ir_mode *amode   = get_ir_mode_arithmetic(mode);
	left             = get_value_as(left, amode);
	right            = get_value_as(right, amode);
	ir_node *div     = new_Div(mem, left, right, amode, op_pin_state_pinned);
	ir_node *new_mem = new_Proj(div, mode_M, pn_Div_M);
	set_store(new_mem);
	ir_node *proj    = new_Proj(div, amode, pn_Div_res);
	return get_value_as(proj, mode);
}

static ir_node *simple_new_Mod(ir_node *left, ir_node *right, ir_mode *mode)
{
	ir_node *mem     = get_store();
	ir_mode *amode   = get_ir_mode_arithmetic(mode);
	left             = get_value_as(left, amode);
	right            = get_value_as(right, amode);
	ir_node *div     = new_Mod(mem, left, right, amode, op_pin_state_pinned);
	ir_node *new_mem = new_Proj(div, mode_M, pn_Div_M);
	set_store(new_mem);
	ir_node *proj    = new_Proj(div, amode, pn_Div_res);
	return get_value_as(proj, mode);
}

static void construct_arith(ir_mode *mode,
		ir_node *(*construct_func)(ir_node *, ir_node *, ir_mode *))
{
	ir_node *right  = symbolic_pop(mode);
	ir_node *left   = symbolic_pop(mode);
	ir_mode *amode  = get_ir_mode_arithmetic(mode);
	left            = get_value_as(left, amode);
	right           = get_value_as(right, amode);
	ir_node *result = construct_func(left, right, amode);
	result          = get_value_as(result, mode);
	symbolic_push(result);
}

static void construct_shift_arith(ir_mode *mode,
		ir_node *(*construct_func)(ir_node *, ir_node *, ir_mode *))
{
	ir_node *right   = symbolic_pop(mode_int);
	ir_node *left    = symbolic_pop(mode);
	ir_mode *amode   = get_ir_mode_arithmetic(mode);
	left             = get_value_as(left, amode);
	right            = get_value_as(right, amode);
	ir_node *right_u = new_Conv(right, mode_Iu);
	ir_node *result  = construct_func(left, right_u, amode);
	result           = get_value_as(result, mode);
	symbolic_push(result);
}

static void construct_arith_unop(ir_mode *mode,
		ir_node *(*construct_func)(ir_node *, ir_mode *))
{
	ir_node *value  = symbolic_pop(mode);
	ir_mode *amode  = get_ir_mode_arithmetic(mode);
	value           = get_value_as(value, amode);
	ir_node *result = construct_func(value, amode);
	result          = get_value_as(result, mode);
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
		snprintf(buf, sizeof(buf), "%"PRId64, (int64_t) val);
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
		ir_type   *klass       = get_class_type(classname);
		ir_entity *rtti_entity = gcji_get_rtti_entity(klass);
		ir_node   *rtti_addr   = new_Address(rtti_entity);

		symbolic_push(rtti_addr);
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
	assert(!needs_two_slots(get_irn_mode(val1)));

	symbolic_push(val1);
	symbolic_push(val1);
	assert((sp+1) == stack_pointer);
}

static void construct_dup_x1(void)
{
	uint16_t sp = stack_pointer;
	ir_node *val1 = symbolic_pop(NULL);
	ir_node *val2 = symbolic_pop(NULL);
	assert(!needs_two_slots(get_irn_mode(val1)));
	assert(!needs_two_slots(get_irn_mode(val2)));

	symbolic_push(val1);
	symbolic_push(val2);
	symbolic_push(val1);
	assert((sp+1) == stack_pointer);
}

static void construct_dup_x2(void)
{
	uint16_t sp = stack_pointer;
	ir_node *val1 = symbolic_pop(NULL);
	ir_node *val2 = symbolic_pop(NULL);
	ir_node *val3 = NULL;

	assert(!needs_two_slots(get_irn_mode(val1)));
	if (!needs_two_slots(get_irn_mode(val2))) {
		val3 = symbolic_pop(NULL);
		assert(!needs_two_slots(get_irn_mode(val3)));
	}
	symbolic_push(val1);
	if (val3 != NULL) symbolic_push(val3);
	symbolic_push(val2);
	symbolic_push(val1);
	assert((sp+1) == stack_pointer);
}

static void construct_dup2(void)
{
	uint16_t sp = stack_pointer;
	ir_node *val1 = symbolic_pop(NULL);
	ir_node *val2 = NULL;
	if (!needs_two_slots(get_irn_mode(val1))) {
		val2 = symbolic_pop(NULL);
		assert(!needs_two_slots(get_irn_mode(val2)));
	}
	if (val2 != NULL) symbolic_push(val2);
	symbolic_push(val1);
	if (val2 != NULL) symbolic_push(val2);
	symbolic_push(val1);

	assert((sp+2) == stack_pointer);
}

static void construct_dup2_x1(void)
{
	uint16_t sp = stack_pointer;
	ir_node *val1 = symbolic_pop(NULL);
	ir_node *val2 = symbolic_pop(NULL);
	ir_node *val3 = NULL;

	assert(!needs_two_slots(get_irn_mode(val2)));
	if (!needs_two_slots(get_irn_mode(val1))) {
		val3 = symbolic_pop(NULL);
		assert(!needs_two_slots(get_irn_mode(val3)));
	}
	if (val3 != NULL) symbolic_push(val2);
	symbolic_push(val1);
	if (val3 != NULL) symbolic_push(val3);
	symbolic_push(val2);
	symbolic_push(val1);
	assert((sp+2) == stack_pointer);
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
	assert(!needs_two_slots(m1)
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

	assert((sp+2) == stack_pointer);
}

static ir_node *create_conv_for_mode_b(ir_node *value, ir_mode *dest_mode)
{
	if (is_Const(value)) {
		if (is_Const_null(value)) {
			return new_Const(get_mode_null(dest_mode));
		} else {
			return new_Const(get_mode_one(dest_mode));
		}
	}

	ir_node *cond       = new_Cond(value);
	ir_node *proj_true  = new_Proj(cond, mode_X, pn_Cond_true);
	ir_node *proj_false = new_Proj(cond, mode_X, pn_Cond_false);
	ir_node *tblock     = new_Block(1, &proj_true);
	ir_node *fblock     = new_Block(1, &proj_false);
	set_cur_block(tblock);
	ir_node *const1 = new_Const(get_mode_one(dest_mode));
	ir_node *tjump  = new_Jmp();
	set_cur_block(fblock);
	ir_node *const0 = new_Const(get_mode_null(dest_mode));
	ir_node *fjump  = new_Jmp();

	ir_node *in[2]      = { tjump, fjump };
	ir_node *mergeblock = new_Block(2, in);
	set_cur_block(mergeblock);
	ir_node *phi_in[2]  = { const1, const0 };
	ir_node *phi        = new_Phi(2, phi_in, dest_mode);
	return phi;
}

static void construct_xcmp(ir_mode *mode, ir_relation rel_1, ir_relation rel_2)
{
	ir_node *val2 = symbolic_pop(mode);
	ir_node *val1 = symbolic_pop(mode);

	ir_mode *amode   = get_ir_mode_arithmetic(mode);
	val1             = get_value_as(val1, amode);
	val2             = get_value_as(val2, amode);
	ir_node *cmp_lt  = new_Cmp(val1, val2, rel_1);
	ir_node *cmp_gt  = new_Cmp(val1, val2, rel_2);
	ir_node *conv_lt = create_conv_for_mode_b(cmp_lt, mode_int);
	ir_node *conv_gt = create_conv_for_mode_b(cmp_gt, mode_int);
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
	ir_node   *addr      = new_Sel(base_addr, 1, in, entity);

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
	ir_node   *addr      = new_Sel(base_addr, 1, in, entity);

	ir_node   *mem       = get_store();
	ir_node   *store     = new_Store(mem, addr, value, cons_none);
	ir_node   *new_mem   = new_Proj(store, mode_M, pn_Store_M);
	set_store(new_mem);
}

static void construct_new_array(ir_type *array_type, ir_node *count)
{
	ir_node  *mem       = get_store();
	ir_type  *elem_type = get_array_element_type(array_type);
	ir_node  *block     = get_cur_block();
	ir_graph *irg       = get_irn_irg(block);
	ir_node  *count_u   = new_Conv(count, mode_Iu);
	ir_node  *res       = gcji_allocate_array(elem_type, count_u, irg, block, &mem);
	set_store(mem);
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

static void code_to_firm(ir_entity *entity, const attribute_code_t *new_code)
{
	code = new_code;

	ir_type *method_type = get_entity_type(entity);

	max_locals = code->max_locals + get_method_n_params(method_type);
	ir_graph *irg = new_ir_graph(entity, max_locals + code->max_stack);
	current_ir_graph = irg;
	stack_pointer    = 0;

	/* static methods need to run static code */
	method_t *method = oo_get_entity_link(entity);
	if (method->access_flags & ACCESS_FLAG_STATIC) {
		ir_type *owner = (ir_type*)class_file->link;
		ir_node *cur_mem = get_store();
		gcji_class_init(owner, get_cur_block(), &cur_mem);
		set_store(cur_mem);
	}

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

		if (!rbitset_is_set(targets, e->handler_pc)) {
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
			// i points to the instruction after the opcode. That instruction
			// should be on a index that is a multiple of 4.
			uint32_t tswitch_index = i-1;
			uint8_t  padding = (4 - (i % 4)) % 4;
			switch (padding) {
			case 3: assert(code->code[i++] == 0); // FALL THROUGH
			case 2: assert(code->code[i++] == 0); // FALL THROUGH
			case 1: assert(code->code[i++] == 0); break;
			default: break;
			}

			int32_t  offset_default = get_32bit_arg(&i);
			uint32_t index_default  = ((int32_t)tswitch_index) + offset_default;

			assert(index_default < code->code_length);

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
			assert(low <= high);
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
			// i points to the instruction after the opcode. That instruction
			// should be on a index that is a multiple of 4.
			uint32_t lswitch_index = i-1;
			uint8_t  padding = (4 - (i % 4)) % 4;
			switch (padding) {
			case 3: assert(code->code[i++] == 0); // FALL THROUGH
			case 2: assert(code->code[i++] == 0); // FALL THROUGH
			case 1: assert(code->code[i++] == 0); break;
			default: break;
			}

			int32_t  offset_default = get_32bit_arg(&i);
			uint32_t index_default  = ((int32_t)lswitch_index) + offset_default;

			assert(index_default < code->code_length);

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

		case OPC_ATHROW:
		case OPC_IRETURN:
		case OPC_LRETURN:
		case OPC_FRETURN:
		case OPC_DRETURN:
		case OPC_ARETURN:
		case OPC_RETURN: {
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

#ifdef EXCEPTIONS
	eh_start_method();
#endif

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

#ifdef EXCEPTIONS
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

					ir_type *catch_type = e->catch_type ? get_classref_type(e->catch_type) : NULL;
					ir_node *handler = get_basic_block(e->handler_pc)->block;
					eh_add_handler(catch_type, handler);
				} else if (e->start_pc > i) {
					break;
				}
			}
		}

		if (rbitset_is_set(try_ends, i))
			eh_pop_lpad();
#else
		if (rbitset_is_set(catch_begins, i))
			symbolic_push(new_Unknown(mode_reference));
#endif

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
				ir_type *owner = get_field_defining_class(index);
				finalize_class_type(owner);

				ir_node  *block  = get_cur_block();
				gcji_class_init(owner, block, &cur_mem);
				addr = new_Address(entity);
			} else {
				ir_node  *object = symbolic_pop(mode_reference);
				addr             = new_simpleSel(object, entity);
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
			finalize_class_type(get_entity_owner(entity));

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
			ir_node *sel     = new_MethodSel(mem, args[0], entity);
			ir_node *sel_mem = new_Proj(sel, mode_M, pn_MethodSel_M);
			ir_node *callee  = new_Proj(sel, mode_reference, pn_MethodSel_res);

#ifdef EXCEPTIONS
			set_store(sel_mem);
			ir_node *call     = eh_new_Call(callee, n_args, args, type);
#else
			ir_node *call     = new_Call(sel_mem, callee, n_args, args, type);
			ir_node *call_mem = new_Proj(call, mode_M, pn_Call_M);
			set_store(call_mem);
#endif

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
			ir_node   *callee = new_Address(entity);
			ir_type   *type   = get_entity_type(entity);
			ir_type   *owner  = get_method_defining_class(index);
			finalize_class_type(owner);
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
			gcji_class_init(owner, block, &cur_mem);
			set_store(cur_mem);

#ifdef EXCEPTIONS
			ir_node *call     = eh_new_Call(callee, n_args, args, type);
#else
			cur_mem  = get_store();
			ir_node *call     = new_Call(cur_mem, callee, n_args, args, type);
			cur_mem = new_Proj(call, mode_M, pn_Call_M);
			set_store(cur_mem);
#endif

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
			ir_node   *callee  = new_Address(entity);
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

#ifdef EXCEPTIONS
			ir_node *call     = eh_new_Call(callee, n_args, args, type);
#else
			ir_node *cur_mem  = get_store();
			ir_node *call     = new_Call(cur_mem, callee, n_args, args, type);
			cur_mem = new_Proj(call, mode_M, pn_Call_M);
			set_store(cur_mem);
#endif

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
			assert(code->code[i++] == 0);
			ir_entity *entity = get_interface_entity(index);
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
			ir_node *sel     = new_MethodSel(mem, args[0], entity);
			ir_node *sel_mem = new_Proj(sel, mode_M, pn_MethodSel_M);
			ir_node *callee  = new_Proj(sel, mode_reference, pn_MethodSel_res);

#ifdef EXCEPTIONS
			set_store(sel_mem);
			ir_node *call     = eh_new_Call(callee, n_args, args, type);
#else
			ir_node *call     = new_Call(sel_mem, callee, n_args, args, type);
			ir_node *call_mem = new_Proj(call, mode_M, pn_Call_M);
			set_store(call_mem);
#endif

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
			finalize_class_type(classtype);
			ir_node  *mem       = get_store();
			ir_node  *block     = get_cur_block();
			ir_node  *result    = gcji_allocate_object(classtype, block, &mem);
			set_store(mem);
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
			finalize_class_type(element_type);
			ir_type *type         = new_type_array(element_type);
			set_type_state(type, layout_fixed);
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
			uint16_t index = get_16bit_arg(&i);
			ir_node *addr  = symbolic_pop(mode_reference);
			ir_type *type  = get_classref_type(index);
			finalize_type(type);

			ir_node *cur_mem = get_store();
			gcji_checkcast(type, addr, irg, get_cur_block(), &cur_mem);
			set_store(cur_mem);

			symbolic_push(addr);
			continue;
		}
		case OPC_INSTANCEOF: {
			uint16_t index      = get_16bit_arg(&i);
			ir_node *addr       = symbolic_pop(mode_reference);

			ir_type *classtype  = get_classref_type(index);
			finalize_class_type(classtype);

			ir_node *cur_mem    = get_store();
			ir_node *instanceof = new_InstanceOf(cur_mem, addr, classtype);
			ir_node *res        = new_Proj(instanceof, mode_b, pn_InstanceOf_res);
			ir_node *conv       = create_conv_for_mode_b(res, mode_Is);
			cur_mem             = new_Proj(instanceof, mode_M, pn_InstanceOf_M);
			set_store(cur_mem);
			symbolic_push(conv);

			continue;
		}
		case OPC_ATHROW: {
			ir_node *addr       = symbolic_pop(mode_reference);
#ifndef EXCEPTIONS
			(void)addr;
			ir_node *abaddr  = new_Address(abort_entity);
			ir_type *abtype  = get_entity_type(abort_entity);
			ir_node *cur_mem = get_store();
			ir_node *call    = new_Call(cur_mem, abaddr, 0, NULL, abtype);
			keep_alive(call);
			keep_alive(get_cur_block());
			set_cur_block(NULL);
#else
			eh_throw(addr);
#endif
			continue;
		}
		case OPC_MONITORENTER:
		case OPC_MONITOREXIT: {
			// FIXME: need real implementation.
			ir_node *addr = symbolic_pop(mode_reference);
			(void) addr;
			continue;
		}
		case OPC_MULTIANEWARRAY: {
			uint16_t index      = get_16bit_arg(&i);
			uint8_t  dims       = code->code[i++];
			ir_node *block      = get_cur_block();
			ir_node *cur_mem    = get_store();
			ir_type *array_type = get_classref_type(index);
			ir_node *array_class_ref
				= gcji_get_runtime_classinfo(array_type, irg, block, &cur_mem);
			ir_node **sizes = XMALLOCN(ir_node *, dims);

			for (int ci = dims-1; ci >= 0; ci--) {
				sizes[ci] = symbolic_pop(mode_int);
			}

			ir_node *marray = gcji_new_multiarray(array_class_ref, dims, sizes,
			                                      irg, block, &cur_mem);
			set_store(cur_mem);

			xfree(sizes);

			symbolic_push(marray);
			continue;
		}

		case OPC_TABLESWITCH: {
			// i points to the instruction after the opcode. That instruction
			// should be on a index that is a multiple of 4.
			const uint32_t tswitch_index = i - 1;
			const uint8_t  padding       = (4 - (i % 4)) % 4;
			switch (padding) {
			case 3: assert(code->code[i++] == 0); // FALL THROUGH
			case 2: assert(code->code[i++] == 0); // FALL THROUGH
			case 1: assert(code->code[i++] == 0); break;
			default: break;
			}

			const int32_t  offset_default = get_32bit_arg(&i);
			const uint32_t index_default  = ((int32_t)tswitch_index) + offset_default;
			assert(index_default < code->code_length);

			const int32_t  low  = get_32bit_arg(&i);
			const int32_t  high = get_32bit_arg(&i);
			assert(low <= high);

			const size_t     ncases      = (high - low + 1);
			ir_switch_table *table       = ir_new_switch_table(current_ir_graph, ncases);
			ir_node         *op          = symbolic_pop(mode_int);
			ir_node         *switch_node = new_Switch(op, ncases + 1, table);
			size_t           case_index  = 0;

			for (int32_t entry = low; entry <= high; entry++, case_index++) {
				const int32_t  offset = get_32bit_arg(&i);
				const uint32_t index  = ((int32_t)tswitch_index) + offset;
				assert(index < code->code_length);

				const long  pn       = pn_Switch_max + 1 + (long)case_index;
				ir_tarval  *case_num = new_tarval_from_long(entry, mode_Is);
				ir_switch_table_set(table, case_index, case_num, case_num, pn);

				ir_node *proj  = new_Proj(switch_node, mode_X, pn);
				ir_node *block = get_target_block_remember_stackpointer(index);
				add_immBlock_pred(block, proj);
			}

			ir_node *def_proj  = new_Proj(switch_node, mode_X, pn_Switch_default);
			ir_node *def_block = get_target_block_remember_stackpointer(index_default);
			add_immBlock_pred(def_block, def_proj);

			set_cur_block(NULL);
			continue;
		}

		case OPC_LOOKUPSWITCH: {
			// i points to the instruction after the opcode. That instruction
			// should be on a index that is a multiple of 4.
			const uint32_t lswitch_index = i - 1;
			const uint8_t  padding       = (4 - (i % 4)) % 4;
			switch (padding) {
			case 3: assert(code->code[i++] == 0); // FALL THROUGH
			case 2: assert(code->code[i++] == 0); // FALL THROUGH
			case 1: assert(code->code[i++] == 0); break;
			default: break;
			}

			const int32_t  offset_default = get_32bit_arg(&i);
			const uint32_t index_default  = ((int32_t)lswitch_index) + offset_default;

			assert(index_default < code->code_length);

			const int32_t    n_pairs     = get_32bit_arg(&i);
			ir_switch_table *table       = ir_new_switch_table(current_ir_graph, n_pairs);
			ir_node         *op          = symbolic_pop(mode_int);
			ir_node         *switch_node = new_Switch(op, n_pairs + 1, table);
			size_t           case_index  = 0;

			for (int pair = 0; pair < n_pairs; pair++, case_index++) {
				const int32_t  match  = get_32bit_arg(&i);
				const int32_t  offset = get_32bit_arg(&i);
				const uint32_t index  = ((int32_t)lswitch_index) + offset;
				assert(index < code->code_length);

				const long  pn       = pn_Switch_max + 1 + (long)pair;
				ir_tarval  *case_num = new_tarval_from_long(match, mode_Is);
				ir_switch_table_set(table, case_index, case_num, case_num, pn);

				ir_node *proj  = new_Proj(switch_node, mode_X, pn);
				ir_node *block = get_target_block_remember_stackpointer(index);
				add_immBlock_pred(block, proj);
			}

			ir_node *def_proj  = new_Proj(switch_node, mode_X, pn_Switch_default);
			ir_node *def_block = get_target_block_remember_stackpointer(index_default);
			add_immBlock_pred(def_block, def_proj);

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

#ifdef EXCEPTIONS
	eh_end_method();
#endif

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

static ir_entity *get_class_member_by_name(ir_type *cls, ident *ident)
{
	for (size_t i = 0, n = get_class_n_members(cls); i < n; ++i) {
		ir_entity *entity = get_class_member(cls, i);
		if (get_entity_ident(entity) == ident)
			return entity;
	}
	return NULL;
}

static ir_entity *find_class_member_in_hierarchy(ir_type *cls, ident *ident)
{
	for (ir_type *t = cls; t != NULL; t = oo_get_class_superclass(t)) {
		ir_entity *entity = get_class_member_by_name(t, ident);
		if (entity != NULL)
			return entity;
	}
	return NULL;
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

	oo_set_entity_link(entity, method);
	method->link = entity;

	const char *classname = get_class_name(owner);
	ident      *ld_id     = mangle_member_name(classname, name, descriptor);
	set_entity_ld_ident(entity, ld_id);


	// determine if we overwrite something
	uint16_t access_flags         = method->access_flags;
	bool     overwrites_something = false;
	if (access_flags & ACCESS_FLAG_ABSTRACT) {
		ir_entity *abstract_method = gcji_get_abstract_method_entity();
		ir_node   *addr            = new_r_Address(get_const_code_irg(), abstract_method);
		set_atomic_ent_value(entity, addr);
	} else if (get_class_n_supertypes(owner) > 0) {
		// see if we overwrite an entity in a superclass
		ir_type   *superclass  = oo_get_class_superclass(owner);
		ir_entity *overwritten
			= find_class_member_in_hierarchy(superclass, mangled_id);
		if (overwritten != NULL) {
			add_entity_overwrites(entity, overwritten);
			oo_set_method_is_inherited(entity, true);
			overwrites_something = true;
		}
	}

	// set access flags
	uint16_t owner_access_flags = class_file->access_flags;
	bool final    = (access_flags | owner_access_flags) & ACCESS_FLAG_FINAL;
	bool abstract = access_flags & ACCESS_FLAG_ABSTRACT;
	oo_set_method_is_abstract(entity, abstract);
	oo_set_method_is_final(entity, final);
	if ((method->access_flags & ACCESS_FLAG_NATIVE) != 0
	    || oo_get_class_is_extern(owner)) {
		set_entity_visibility(entity, ir_visibility_external);
	}

	// decide binding mode
	bool is_constructor = strncmp(name, "<init>", 6) == 0;
	bool exclude_from_vtable =
	   ((access_flags & ACCESS_FLAG_STATIC)
	 || (access_flags & ACCESS_FLAG_PRIVATE)
	 || (is_constructor)
	 || ((access_flags & ACCESS_FLAG_FINAL) && !overwrites_something));
	oo_set_method_exclude_from_vtable(entity, exclude_from_vtable);

	ddispatch_binding binding = bind_unknown;
	if (exclude_from_vtable || (owner_access_flags & ACCESS_FLAG_FINAL)
	    || (access_flags & ACCESS_FLAG_FINAL))
		binding = bind_static;
	else if (owner_access_flags & ACCESS_FLAG_INTERFACE)
		binding = bind_interface;
	else
		binding = bind_dynamic;

	oo_set_entity_binding(entity, binding);

}

static void create_method_code(ir_entity *entity)
{
	assert(is_Method_type(get_entity_type(entity)));
	if (verbose)
		fprintf(stderr, "...%s\n", get_entity_name(entity));

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
	ir_type *existing_type = class_registry_get(name);
	if (existing_type != NULL)
		return existing_type;

	if (verbose)
		fprintf(stderr, "==> reading class %s\n", name);

	class_t *cls = read_class(name);
	if (cls == NULL)
		panic("Couldn't find class %s", name);

	ident   *class_ident = new_id_from_str(name);
	ir_type *type        = new_type_class(class_ident);
	class_registry_set(name, type);
	oo_set_type_link(type, cls);
	cls->link = type;

	if (strcmp(name, "java/lang/Class") == 0) {
		gcji_set_java_lang_class(type);
	} else if (strcmp(name, "java/lang/Object") == 0) {
		gcji_set_java_lang_object(type);
	}

	/* set access mode/flags */
	oo_set_class_is_final(type,     cls->access_flags & ACCESS_FLAG_FINAL);
	oo_set_class_is_abstract(type,  cls->access_flags & ACCESS_FLAG_ABSTRACT);
	oo_set_class_is_interface(type, cls->access_flags & ACCESS_FLAG_INTERFACE);
	oo_set_class_is_extern(type,    cls->is_extern);

	/* create vtable entity (the actual initializer will be created in liboo) */
	if (!(cls->access_flags & ACCESS_FLAG_INTERFACE))
		gcji_create_vtable_entity(type);
	/* create rtti entity */
	gcji_create_rtti_entity(type);

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
	if (verbose)
		fprintf(stderr, "==> Construct methods of %s\n", get_class_name(type));

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

/**
 * If an interface gets implemented, then we replicate all methods in the class
 * type. Note that we even do this for missing methods in abstract classes
 * implementing an interface.
 */
static void link_interface_methods(ir_type *cls, ir_type *interface)
{
	assert(oo_get_class_is_interface(interface));
	if (oo_get_class_is_interface(cls))
		return;

	for (size_t m = 0, n = get_class_n_members(interface); m < n; ++m) {
		ir_entity *method = get_class_member(interface, m);
		if (!is_method_entity(method)) continue;
		assert(!oo_get_method_exclude_from_vtable(method));
		ident     *ident          = get_entity_ident(method);
		ir_entity *implementation = find_class_member_in_hierarchy(cls, ident);
		// find the implementation of the method (or return abstract_method)
		bool add_overwrites = true;
		if (implementation == NULL) {
			if (!oo_get_class_is_abstract(cls)) {
				panic("%s: implementation of method %s from interface %s missing in non-abstract class\n", get_class_name(cls), get_id_str(ident), get_class_name(interface));
			} else {
				implementation = gcji_get_abstract_method_entity();
				add_overwrites = false;
			}
		}
		// create proxy entity in cls if necessary
		if (get_entity_owner(implementation) != cls) {
			ir_type   *type = get_entity_type(method);
			ir_entity *proxy = new_entity(cls, ident, type);
			set_entity_ld_ident(proxy, id_mangle3("inh__", get_entity_ld_ident(implementation), ""));
			set_entity_visibility(proxy, ir_visibility_private);
			oo_copy_entity_info(implementation, proxy);

			set_atomic_ent_value(proxy, get_atomic_ent_value(implementation));
			if (add_overwrites) {
				oo_set_method_is_inherited(proxy, true);
				add_entity_overwrites(proxy, implementation);
			}
			implementation = proxy;
		}

		add_entity_overwrites(implementation, method);
	}
}

static void enqueue_class(ir_type *type)
{
	class_t *cls = (class_t*)oo_get_type_link(type);
	if (cls->in_construction_queue)
		return;
	pdeq_putr(worklist, type);
	cls->in_construction_queue = true;
}

static void finalize_class_type(ir_type *type)
{
	assert(is_Class_type(type));
	class_t *cls = (class_t*)oo_get_type_link(type);
	if (cls->constructed)
		return;
	cls->constructed = true;

	class_t *old_class_file = class_file;
	class_file = cls;

	/* add supertype and setup vptr */
	if (class_file->super_class != 0) {
		ir_type *supertype = get_classref_type(class_file->super_class);
		assert(supertype != type);
		finalize_class_type(supertype);

		add_class_supertype(type, supertype);
		/* the supertype data is contained in the object so we create a field
		 * that contains this data */
		new_entity(type, subobject_ident, supertype);

		/* use the same vptr field as our superclass */
		ir_entity *vptr = oo_get_class_vptr_entity(supertype);
		oo_set_class_vptr_entity(type, vptr);
	} else {
		/* java.lang.Object is the only class without a superclass */
		assert(strcmp(get_class_name(type), "java/lang/Object") == 0);

		/* create a new vptr field */
		ident     *vptr_ident = new_id_from_str("@vptr");
		ir_entity *vptr       = new_entity(type, vptr_ident, type_reference);
		oo_set_class_vptr_entity(type, vptr);
	}

	for (size_t f = 0; f < (size_t) class_file->n_fields; ++f) {
		field_t *field = class_file->fields[f];
		create_field_entity(field, type);
	}
	if (strcmp(get_class_name(type), "java/lang/Class") == 0) {
		gcji_add_java_lang_class_fields(type);
		/* now is a good time to create the class types */
		gcji_create_array_type();
	}

	for (size_t m = 0; m < (size_t) class_file->n_methods; ++m) {
		method_t *method = class_file->methods[m];
		create_method_entity(method, type);
	}

	for (size_t i = 0; i < (size_t) class_file->n_interfaces; ++i) {
		ir_type *iface = get_classref_type(class_file->interfaces[i]);
		finalize_class_type(iface);
		add_class_supertype(type, iface);

		link_interface_methods(type, iface);
	}

	default_layout_compound_type(type);

	ddispatch_setup_vtable(type);
	/* RTTI from external classes is already created */
	if (!cls->is_extern) {
		gcji_setup_rtti_entity(cls, type);
	}

	// make sure the methods are constructed later
	enqueue_class(type);

	assert(class_file == cls);
	class_file = old_class_file;
}

static void finalize_type(ir_type *type)
{
	if (is_Pointer_type(type)) {
		finalize_type(get_pointer_points_to_type(type));
	} else if (is_Class_type(type)) {
		finalize_class_type(type);
	} else if (is_Array_type(type)) {
		finalize_type(get_array_element_type(type));
	}
}

static void remove_external_vtable(ir_type *type, void *env)
{
	(void) env;
	if (oo_get_class_is_extern(type)) {
		ir_entity *vtable = oo_get_class_vtable_entity(type);
		if (vtable != NULL) {
			set_entity_initializer(vtable, NULL);
			set_entity_type(vtable, get_unknown_type());
		}
	}
}

int main(int argc, char **argv)
{
	gen_firm_init();
	init_types();
	class_registry_init();
	mangle_init();
	oo_init();
	gcji_init();
	class_file_init();

	if (argc < 2) {
		fprintf(stderr, "Syntax: %s [-cp <classpath>] [-bootclasspath <bootclasspath>] [-externclasspath] [-o <output file name>] [-f <firm option>]* class_file\n", argv[0]);
		return 0;
	}

	const char *output_name   = NULL;
	bool        save_temps    = false;
	bool        static_stdlib = false;
	enum {
		RUNTIME_GCJ,
		RUNTIME_SIMPLERT
	} runtime_type = RUNTIME_SIMPLERT;

	int curarg = 1;
#define EQUALS(x)             (strcmp(x, argv[curarg]) == 0)
#define EQUALS_AND_HAS_ARG(x) ((strcmp(x, argv[curarg]) == 0 && (curarg+1) < argc))
#define ARG_PARAM             (argv[++curarg])
	while (curarg < argc) {
		if (EQUALS_AND_HAS_ARG("-cp") || EQUALS_AND_HAS_ARG("--classpath")) {
			classpath_prepend(ARG_PARAM, false);
		} else if (EQUALS_AND_HAS_ARG("-bootclasspath")) {
			classpath_append(ARG_PARAM, false);
		} else if (EQUALS_AND_HAS_ARG("-externcclasspath")) {
			classpath_append(ARG_PARAM, true);
		} else if (EQUALS_AND_HAS_ARG("-o")) {
			output_name = ARG_PARAM;
		} else if (EQUALS("--static-stdlib")) {
			static_stdlib = true;
		} else if (EQUALS_AND_HAS_ARG("-f")) {
			const char *param = ARG_PARAM;
			if (!firm_option(param))
				fprintf(stderr, "Warning: '%s' is not a valid Firm option - ignoring.\n", param);
		} else if (EQUALS_AND_HAS_ARG("-b")) {
			const char *param = ARG_PARAM;
			if (!be_parse_arg(param))
				fprintf(stderr, "Warning: '%s' is not a valid backend option - ignoring.\n", param);
		} else if (EQUALS("-save-temps")) {
			save_temps = true;
		} else if (EQUALS("-v")) {
			verbose = true;
		} else if (EQUALS("--simplert")) {
			runtime_type = RUNTIME_SIMPLERT;
		} else if (EQUALS("--gcj")) {
			runtime_type = RUNTIME_GCJ;
		} else {
			main_class_name = argv[curarg];
		}
		curarg++;
	}
#undef ARG_PARAM
#undef EQUALS_AND_HAS_ARG
#undef EQUALS
#undef SINGLE_OPTION

	if (main_class_name == NULL) {
		fprintf(stderr, "No main class specified!\n");
		return 1;
	}
	size_t arg_len        = strlen(main_class_name);
	main_class_name_short = main_class_name + arg_len - 1;
	while (main_class_name_short > main_class_name && *(main_class_name_short-1) != '/') main_class_name_short--;

	if (!output_name)
		output_name = main_class_name_short;

	if (runtime_type == RUNTIME_GCJ) {
		classpath_append(CLASSPATH_GCJ, true);
	}
	if (runtime_type == RUNTIME_SIMPLERT) {
		classpath_append(CLASSPATH_SIMPLERT, false);
	}
	if (verbose)
		classpath_print(stderr);

	worklist = new_pdeq();

	/* read java.lang.Class first - this type is needed to construct RTTI
	 * information */
	ir_type *java_lang_class = get_class_type("java/lang/Class");
	enqueue_class(java_lang_class);

	/* trigger loading of the class specified on commandline */
	ir_type *main_class = get_class_type(main_class_name);
	enqueue_class(main_class);

	while (!pdeq_empty(worklist)) {
		ir_type *classtype = pdeq_getl(worklist);

		if (!oo_get_class_is_extern(classtype)) {
			finalize_class_type(classtype);
			construct_class_methods(classtype);
		}
	}
	/* if java/lang/Class is external, then we might not have constructed it
	 * yet, but we need to do this in this special case as the gcji stuff
	 * produces instances of it
	 */
	finalize_class_type(java_lang_class);
	irp_finalize_cons();

	/* verify the constructed graphs, entities and types */
	for (size_t i = 0, n = get_irp_n_irgs(); i < n; ++i) {
		ir_graph *irg = get_irp_irg(i);
		irg_assert_verify(irg);
	}
	int res = tr_verify();
	assert(res != 0);

	oo_lower();
	/* kinda hacky: we remove vtables for external classes now
	 * (we constructed them in the first places because we needed the vtable_ids
	 *  for the methods in non-external subclasses)
	 */
	class_walk_super2sub(remove_external_vtable, NULL, NULL);

	if (verbose)
		fprintf(stderr, "===> Optimization & backend\n");

	ir_entity *main_rtti = gcji_get_rtti_entity(main_class);
	assert(main_rtti && is_entity(main_rtti));
	const char *main_rtti_ldname = get_entity_ld_name(main_rtti);

	char startup_file[] = "bc2firm_startup_XXXXXX";
	int startup_fd = mkstemp(startup_file);
	FILE *startup_out = fdopen(startup_fd, "w");
	fprintf(startup_out, "extern void JvRunMain(void* klass, int argc, const char **argv);\n");
	fprintf(startup_out, "extern void *%s;\n", main_rtti_ldname);
	fprintf(startup_out, "int main(int argc, const char **argv) { JvRunMain(&%s, argc, argv); return 0; }\n", main_rtti_ldname);
	fclose(startup_out);

	char asm_file[] = "bc2firm_asm_XXXXXX";
	int asm_fd = mkstemp(asm_file);
	FILE *asm_out = fdopen(asm_fd, "w");

#ifdef EXCEPTIONS
	be_parse_arg("omitfp=false");
	be_parse_arg("ia32-emit_cfi_directives");
#endif

	be_dwarf_set_source_language(DW_LANG_Java);
	generate_code(asm_out, main_class_name);

	gen_firm_finish();

	fclose(asm_out);

	class_file_exit();
	gcji_deinit();
	oo_deinit();
	mangle_deinit();

	char cmd_buffer[1024];
	if (runtime_type == RUNTIME_GCJ) {
		/* libgcj */
		sprintf(cmd_buffer, "gcc -m32 -g -x assembler %s -x c %s -x none -lgcj -lstdc++ -o %s", asm_file, startup_file, output_name);
	} else {
		/* simplert */
		if (static_stdlib) {
			sprintf(cmd_buffer, "gcc -m32 -g -x assembler %s -x c %s -x none %s/simplert.a -o %s", asm_file, startup_file, CLASSPATH_SIMPLERT, output_name);
		} else {
			sprintf(cmd_buffer, "gcc -m32 -g -x assembler %s -x c %s -x none -L%s -lsimplert -Wl,-R%s -o %s", asm_file, startup_file, CLASSPATH_SIMPLERT, CLASSPATH_SIMPLERT, output_name);
		}
	}

	if (verbose)
		fprintf(stderr, "===> Assembling & linking (%s)\n", cmd_buffer);

	int retval = system(cmd_buffer);

	if (!save_temps) {
		remove(startup_file);
		remove(asm_file);
	}

	if (verbose)
		fprintf(stderr, "===> Done!\n");

	return retval;
}
