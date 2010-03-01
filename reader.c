#include "class_file.h"
#include "opcodes.h"
#include "lower_oo.h"

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

#include <libfirm/firm.h>

#define VERBOSE

static pdeq    *worklist;
static class_t *class_file;

static const constant_t *get_constant(uint16_t index)
{
	assert(index < class_file->n_constants);
	const constant_t *constant = class_file->constants[index];
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

static ir_mode *mode_byte;
static ir_mode *mode_int;
static ir_mode *mode_reference;

static ir_type *type_byte;
static ir_type *type_char;
static ir_type *type_short;
static ir_type *type_int;
static ir_type *type_long;
static ir_type *type_boolean;
static ir_type *type_float;
static ir_type *type_double;

static void init_types(void)
{
	mode_byte
		= new_ir_mode("B", irms_int_number, 8, 1, irma_twos_complement, 32);
	type_byte = new_type_primitive(mode_byte);

	ir_mode *mode_char
		= new_ir_mode("C", irms_int_number, 16, 0, irma_twos_complement, 0);
	type_char = new_type_primitive(mode_char);

	ir_mode *mode_short
		= new_ir_mode("S", irms_int_number, 16, 1, irma_twos_complement, 32);
	type_short = new_type_primitive(mode_short);

	mode_int
		= new_ir_mode("I", irms_int_number, 32, 1, irma_twos_complement, 32);
	type_int = new_type_primitive(mode_int);

	ir_mode *mode_long
		= new_ir_mode("J", irms_int_number, 64, 1, irma_twos_complement, 32);
	type_long = new_type_primitive(mode_long);

	ir_mode *mode_boolean
		= new_ir_mode("Z", irms_int_number, 8, 1, irma_twos_complement, 0);
	type_boolean = new_type_primitive(mode_boolean);

	ir_mode *mode_float
		= new_ir_mode("F", irms_float_number, 32, 1, irma_ieee754, 0);
	type_float = new_type_primitive(mode_float);

	ir_mode *mode_double
		= new_ir_mode("D", irms_float_number, 64, 1, irma_ieee754, 0);
	type_double = new_type_primitive(mode_double);

	mode_reference = mode_P;
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
		set_type_link(type, NULL);

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
	ir_type *type = class_registry_get(get_id_str(id));

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

#ifdef VERBOSE
	fprintf(stderr, "Field %s\n", name);
#endif
}

static const attribute_code_t *code;
static uint16_t                stack_pointer;
static uint16_t                max_locals;

static void symbolic_push(ir_node *node)
{
	if (stack_pointer >= code->max_stack)
		panic("code exceeds stack limit");
	set_value(stack_pointer++, node);
}

static ir_node *symbolic_pop(ir_mode *mode)
{
	if (stack_pointer == 0)
		panic("code produces stack underflow");
	return get_value(--stack_pointer, mode);
}

static void set_local(uint16_t n, ir_node *node)
{
	assert(n < max_locals);
	set_value(code->max_stack + n, node);
}

static ir_node *get_local(uint16_t n, ir_mode *mode)
{
	assert(n < max_locals);
	return get_value(code->max_stack + n, mode);
}

static ir_node *create_symconst(ir_entity *entity)
{
	union symconst_symbol sym;
	sym.entity_p = entity;
	return new_SymConst(mode_reference, sym, symconst_addr_ent);
}

static ir_entity *get_method_entity(uint16_t methodref_index)
{
	const constant_t *methodref = get_constant(methodref_index);
	if (methodref->kind != CONSTANT_METHODREF) {
		panic("get_method_entity index argumetn not a methodref");
	}
	const constant_t *name_and_type 
		= get_constant(methodref->methodref.name_and_type_index);
	if (name_and_type->kind != CONSTANT_NAMEANDTYPE) {
		panic("invalid name_and_type in method %u", methodref_index);
	}
	const char *methodname
		= get_constant_string(name_and_type->name_and_type.name_index);

	const constant_t *classref
		= get_constant(methodref->methodref.class_index);
	if (classref->kind != CONSTANT_CLASSREF) {
		panic("invalid classref in method %u", methodref_index);
	}
	const char *classname
		= get_constant_string(classref->classref.name_index);
	ir_type *classtype = get_class_type(classname);

	/* TODO: walk class hierarchy */
	/* TODO: we could have a field with the same name */
	ident     *methodid = new_id_from_str(methodname);
	ir_entity *entity   = get_class_member_by_name(classtype, methodid);
	assert(is_method_entity(entity));

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
	for (int i = 0; i < n_parameters; ++i) {
		ir_type *type = get_method_param_type(method_type, i);
		ir_node *val  = new_Proj(args, get_type_mode(type), i);
		set_local(i, val);
	}

	/* pass1: identify jump targets and create blocks for them */
	unsigned *targets = rbitset_malloc(code->code_length);
	basic_blocks = NEW_ARR_F(basic_block_t, 0);

	basic_block_t start;
	start.pc            = 0;
	start.block         = first_block;
	start.stack_pointer = 0;
	ARR_APP1(basic_block_t, basic_blocks, start);
	rbitset_set(targets, 0);

	for (uint32_t i = 0; i < code->code_length; /* nothing */) {
		opcode_kind_t opcode = code->code[i++];
		switch(opcode) {
		case OPC_BIPUSH:
		case OPC_ALOAD:
		case OPC_ILOAD:
		case OPC_ISTORE:
			i++;
			break;

		case OPC_GETSTATIC:
		case OPC_GETFIELD:
		case OPC_INVOKEVIRTUAL:
		case OPC_INVOKESTATIC:
		case OPC_INVOKESPECIAL:
			i+=2;
			break;

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
		case OPC_ICMPGE: {
			uint8_t  b1 = code->code[i++];
			uint8_t  b2 = code->code[i++];
			uint16_t index = (b1 << 8) | b2;
			index += (i-3);

			assert(index < code->code_length);
			if (!rbitset_is_set(targets, index)) {
				rbitset_set(targets, index);

				basic_block_t target;
				target.pc            = index;
				target.block         = new_immBlock();
				target.stack_pointer = -1;
				ARR_APP1(basic_block_t, basic_blocks, target);
			}

			if (opcode != OPC_GOTO) {
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
			break;
		}

		case OPC_ICONST_M1:
		case OPC_ICONST_0:
		case OPC_ICONST_1:
		case OPC_ICONST_2:
		case OPC_ICONST_3:
		case OPC_ICONST_4:
		case OPC_ICONST_5:
		case OPC_ALOAD_0:
		case OPC_ALOAD_1:
		case OPC_ALOAD_2:
		case OPC_ALOAD_3:
		case OPC_ILOAD_0:
		case OPC_ILOAD_1:
		case OPC_ILOAD_2:
		case OPC_ILOAD_3:
		case OPC_ISTORE_0:
		case OPC_ISTORE_1:
		case OPC_ISTORE_2:
		case OPC_ISTORE_3:
		case OPC_RETURN:
		case OPC_IRETURN:
		case OPC_IADD:
		case OPC_ISUB:
			break;
		}
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

		opcode_kind_t opcode = code->code[i++];
		switch (opcode) {
		case OPC_ICONST_M1:symbolic_push(new_Const_long(mode_int,-1)); continue;
		case OPC_ICONST_0: symbolic_push(new_Const_long(mode_int, 0)); continue;
		case OPC_ICONST_1: symbolic_push(new_Const_long(mode_int, 1)); continue;
		case OPC_ICONST_2: symbolic_push(new_Const_long(mode_int, 2)); continue;
		case OPC_ICONST_3: symbolic_push(new_Const_long(mode_int, 3)); continue;
		case OPC_ICONST_4: symbolic_push(new_Const_long(mode_int, 4)); continue;
		case OPC_ICONST_5: symbolic_push(new_Const_long(mode_int, 5)); continue;
		case OPC_BIPUSH: {
			int8_t val = code->code[i++];
			symbolic_push(new_Const_long(mode_int, val));
			continue;
		}

		case OPC_ALOAD_0: symbolic_push(get_local(0, mode_reference)); continue;
		case OPC_ALOAD_1: symbolic_push(get_local(1, mode_reference)); continue;
		case OPC_ALOAD_2: symbolic_push(get_local(2, mode_reference)); continue;
		case OPC_ALOAD_3: symbolic_push(get_local(3, mode_reference)); continue;
		case OPC_ALOAD: {
			uint8_t index = code->code[i++];
			symbolic_push(get_local(index, mode_reference));
			continue;
		}

		case OPC_ILOAD_0: symbolic_push(get_local(0, mode_int)); continue;
		case OPC_ILOAD_1: symbolic_push(get_local(1, mode_int)); continue;
		case OPC_ILOAD_2: symbolic_push(get_local(2, mode_int)); continue;
		case OPC_ILOAD_3: symbolic_push(get_local(3, mode_int)); continue;
		case OPC_ILOAD: {
			uint8_t index = code->code[i++];
			symbolic_push(get_local(index, mode_int));
			continue;
		}

		case OPC_ISTORE_0: set_local(0, symbolic_pop(mode_int)); continue;
		case OPC_ISTORE_1: set_local(1, symbolic_pop(mode_int)); continue;
		case OPC_ISTORE_2: set_local(2, symbolic_pop(mode_int)); continue;
		case OPC_ISTORE_3: set_local(3, symbolic_pop(mode_int)); continue;
		case OPC_ISTORE: {
			uint8_t index = code->code[i++];
			set_local(index, symbolic_pop(mode_int));
			continue;
		}

		case OPC_IRETURN: {
			ir_type *return_type = get_method_res_type(method_type, 0);
			ir_mode *res_mode    = get_type_mode(return_type);
			ir_node *val         = symbolic_pop(mode_int);
			val = new_Conv(val, res_mode);
			ir_node *in[1] = { val };
			ir_node *ret   = new_Return(get_store(), 1, in);

			if (stack_pointer != 0) {
				fprintf(stderr,
				        "Warning: stackpointer >0 after ireturn at %u\n", i);
			}
			
			ir_node *end_block = get_irg_end_block(current_ir_graph);
			add_immBlock_pred(end_block, ret);
			set_cur_block(new_Bad());
			continue;
		}

		case OPC_RETURN: {
			if (stack_pointer != 0) {
				fprintf(stderr,
				        "Warning: stackpointer >0 after return at %u\n", i);
			}

			ir_node *ret       = new_Return(get_store(), 0, NULL);
			ir_node *end_block = get_irg_end_block(current_ir_graph);
			add_immBlock_pred(end_block, ret);
			set_cur_block(new_Bad());
			continue;
		}

		case OPC_IADD: {
			ir_node *val2 = symbolic_pop(mode_int);
			ir_node *val1 = symbolic_pop(mode_int);
			symbolic_push(new_Add(val1, val2, mode_int));
			continue;
		}

		case OPC_ISUB: {
			ir_node *val2 = symbolic_pop(mode_int);
			ir_node *val1 = symbolic_pop(mode_int);
			symbolic_push(new_Sub(val1, val2, mode_int));
			continue;
		}

		case OPC_GOTO: {
			uint8_t  b1    = code->code[i++];
			uint8_t  b2    = code->code[i++];
			uint16_t index = (b1 << 8) | b2;
			index += (i-3);

			ir_node *jmp = new_Jmp();
			ir_node *target_block 
				= get_target_block_remember_stackpointer(index);
			add_immBlock_pred(target_block, jmp);

			set_cur_block(NULL);
			continue;
		}

		case OPC_ACMPEQ:
		case OPC_ACMPNE:
		case OPC_IFNULL:
		case OPC_IFNONNULL: {
			uint8_t  b1    = code->code[i++];
			uint8_t  b2    = code->code[i++];
			uint16_t index = (b1 << 8) | b2;
			index += (i-3);

			ir_node *val1 = symbolic_pop(mode_reference);
			ir_node *val2;
			if (opcode == OPC_IFNULL || opcode == OPC_IFNONNULL) {
				val2 = new_Const_long(mode_int, 0);
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
			uint8_t  b1    = code->code[i++];
			uint8_t  b2    = code->code[i++];
			uint16_t index = (b1 << 8) | b2;
			index += (i-3);

			ir_node *val1 = symbolic_pop(mode_int);
			ir_node *val2;
			if (opcode >= OPC_IFEQ && opcode <= OPC_IFLE) {
				val2 = new_Const_long(mode_int, 0);
			} else {
				assert(opcode >= OPC_ICMPEQ && opcode <= OPC_ICMPLE);
				val2 = symbolic_pop(mode_int);
			}
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

		case OPC_INVOKESPECIAL: {
			uint8_t    b1      = code->code[i++];
			uint8_t    b2      = code->code[i++];
			uint16_t   index   = (b1 << 8) | b2;
			ir_entity *entity  = get_method_entity(index);
			ir_node   *callee  = create_symconst(entity);
			/* TODO: construct real arguments */
			ir_node   *args[1] = { symbolic_pop(mode_reference) };
			ir_node   *mem     = get_store();
			ir_type   *type    = get_entity_type(entity);
			ir_node   *call    = new_Call(mem, callee, 1, args, type);

			ir_node   *new_mem = new_Proj(call, mode_M, pn_Call_M);
			set_store(new_mem);
			
			continue;
		}

		case OPC_GETSTATIC:
		case OPC_GETFIELD:
		case OPC_INVOKEVIRTUAL:
		case OPC_INVOKESTATIC:
			panic("Unimplemented opcode 0x%X found\n", opcode);
		}

		panic("Unimplemented opcode 0x%X found\n", opcode);
	}

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

	dump_ir_block_graph(irg, "");
}

static void create_method_entity(method_t *method, ir_type *owner)
{
	const char *name       = get_constant_string(method->name_index);
	const char *descriptor = get_constant_string(method->descriptor_index);
	ident      *id         = new_id_from_str(name);
	ir_type    *type       = method_descriptor_to_type(descriptor, owner,
	                                                   method->access_flags);
	ir_entity  *entity     = new_entity(owner, id, type);
	set_entity_link(entity, method);
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
		const constant_t *classref = get_constant(class_file->super_class);
		assert(classref->kind == CONSTANT_CLASSREF);
		const char *supertype_name 
			= get_constant_string(classref->classref.name_index);

		ir_type *supertype = get_class_type(supertype_name);
		assert (supertype != type);
		add_class_supertype(type, supertype);
	} else {
		/* this should only happen for java.lang.Object */
		assert(strcmp(name, "java/lang/Object") == 0);
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

	const char *classpath = "classes/";
	class_file_init(classpath);
	worklist = new_pdeq();

	if (argc < 2) {
		fprintf(stderr, "Syntax: %s class_file\n", argv[0]);
		return 0;
	}

	/* trigger loading of the class specified on commandline */
	get_class_type(argv[1]);

	while (!pdeq_empty(worklist)) {
		ir_type *classtype = pdeq_getl(worklist);
		construct_class_methods(classtype);
	}

	irp_finalize_cons();
	lower_oo();

	int n_irgs = get_irp_n_irgs();
	for (int p = 0; p < n_irgs; ++p) {
		ir_graph *irg = get_irp_irg(p);
		optimize_reassociation(irg);
		optimize_load_store(irg);
		optimize_graph_df(irg);
		place_code(irg);
		optimize_cf(irg);
		opt_if_conv(irg, be_params->if_conv_info);
		optimize_cf(irg);
		optimize_reassociation(irg);
		optimize_graph_df(irg);
		opt_jumpthreading(irg);
		optimize_graph_df(irg);
		optimize_cf(irg);
		/* TODO: This shouldn't be needed but the backend sometimes finds
		   dead Phi nodes if we don't do this */
		edges_deactivate(irg);
	}

	be_parse_arg("omitfp");
	be_main(stdout, "bytecode");

	class_file_exit();

	return 0;
}
