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

#include <libfirm/firm.h>

#define VERBOSE

static pdeq    *worklist;
static class_t *class_file;

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

	mode_long
		= new_ir_mode("J", irms_int_number, 64, 1, irma_twos_complement, 32);
	type_long = new_type_primitive(mode_long);

	ir_mode *mode_boolean
		= new_ir_mode("Z", irms_int_number, 8, 1, irma_twos_complement, 0);
	type_boolean = new_type_primitive(mode_boolean);

	mode_float
		= new_ir_mode("F", irms_float_number, 32, 1, irma_ieee754, 0);
	type_float = new_type_primitive(mode_float);

	mode_double
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

	if (field->access_flags & ACCESS_FLAG_STATIC) {
		set_entity_allocation(entity, allocation_static);
	}

#ifdef VERBOSE
	fprintf(stderr, "Field %s\n", name);
#endif
}

static const attribute_code_t *code;
static uint16_t                stack_pointer;
static uint16_t                max_locals;

static bool needs_two_slots(ir_mode *mode)
{
	return mode == mode_long || mode == mode_double;
}

static void symbolic_push(ir_node *node)
{
	if (stack_pointer >= code->max_stack)
		panic("code exceeds stack limit");
	ir_mode *mode = get_irn_mode(node);
	/* double and long need 2 stackslots */
	if (needs_two_slots(mode))
		set_value(stack_pointer++, new_Bad());

	set_value(stack_pointer++, node);
}

static ir_node *symbolic_pop(ir_mode *mode)
{
	if (stack_pointer == 0)
		panic("code produces stack underflow");

	ir_node *result = get_value(--stack_pointer, mode);
	/* double and long need 2 stackslots */
	if (needs_two_slots(mode)) {
		ir_node *dummy = get_value(--stack_pointer, mode);
		(void) dummy;
		assert(is_Bad(dummy));
	}

	return result;
}

static void set_local(uint16_t n, ir_node *node)
{
	assert(n < max_locals);
	set_value(code->max_stack + n, node);
	ir_mode *mode = get_irn_mode(node);
	if (needs_two_slots(mode)) {
		assert(n+1 < max_locals);
		set_value(code->max_stack + n+1, new_Bad());
	}
}

static ir_node *get_local(uint16_t n, ir_mode *mode)
{
	if (needs_two_slots(mode)) {
		assert(n+1 < max_locals);
		ir_node *dummy = get_value(code->max_stack + n+1, mode);
		(void) dummy;
		assert(is_Bad(dummy));
	}
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
	constant_t *methodref = get_constant(methodref_index);
	if (methodref->kind != CONSTANT_METHODREF) {
		panic("get_method_entity index argumetn not a methodref");
	}
	ir_entity *entity = methodref->base.link;
	if (entity == NULL) {
		const constant_t *name_and_type 
			= get_constant(methodref->methodref.name_and_type_index);
		if (name_and_type->kind != CONSTANT_NAMEANDTYPE) {
			panic("invalid name_and_type in method %u", methodref_index);
		}
		ir_type *classtype 
			= get_classref_type(methodref->methodref.class_index);

		/* TODO: walk class hierarchy */
		/* TODO: we could have a field with the same name */
		const char *methodname
			= get_constant_string(name_and_type->name_and_type.name_index);
		ident *methodid = new_id_from_str(methodname);
		entity = get_class_member_by_name(classtype, methodid);
		assert(is_method_entity(entity));
		methodref->base.link = entity;
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
		float    val   = *((float*) &constant->floatc.value);
		tarval  *tv    = new_tarval_from_double(val, mode_float);
		ir_node *cnode = new_Const(tv);
		symbolic_push(cnode);
		break;
	}
	case CONSTANT_STRING:
		panic("string constant not implemented yet");
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
	set_cur_block(new_Bad());
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
			i++;
			break;

		case OPC_SIPUSH:
		case OPC_LDC_W:
		case OPC_LDC2_W:
		case OPC_IINC:
		case OPC_GETSTATIC:
		case OPC_GETFIELD:
		case OPC_INVOKEVIRTUAL:
		case OPC_INVOKESTATIC:
		case OPC_INVOKESPECIAL:
		case OPC_NEW:
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
		case OPC_IRETURN:
		case OPC_LRETURN:
		case OPC_FRETURN:
		case OPC_DRETURN:
		case OPC_ARETURN:
		case OPC_RETURN:
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
		case OPC_SIPUSH: {
			uint16_t val = code->code[i++];
			val = (val << 8) | code->code[i++];
			push_const(mode_int, (int16_t) code->code[i++]);
			continue;
		}

		case OPC_LDC:       push_load_const(code->code[i++]); continue;
		case OPC_LDC2_W:
		case OPC_LDC_W: {
			uint8_t  b1    = code->code[i++];
			uint8_t  b2    = code->code[i++];
			uint16_t index = (b1 << 8) | b2;
			push_load_const(index);
			continue;
		}

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

		case OPC_ISTORE:   pop_set_local(code->code[i++], mode_int);   continue;
		case OPC_LSTORE:   pop_set_local(code->code[i++], mode_long);  continue;
		case OPC_FSTORE:   pop_set_local(code->code[i++], mode_float); continue;
		case OPC_DSTORE:   pop_set_local(code->code[i++], mode_double);continue;
		case OPC_ASTORE:   pop_set_local(code->code[i++], mode_reference); continue;

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

		case OPC_POP:  --stack_pointer;    continue;
		case OPC_POP2: stack_pointer -= 2; continue;

		case OPC_DUP2:
		case OPC_DUP: {
			/* TODO: this only works for values defined in the same block */
			ir_mode *mode = opcode == OPC_DUP2 ? mode_long : NULL;
			ir_node *top  = symbolic_pop(mode);
			symbolic_push(top);
			symbolic_push(top);
			continue;
		}
		case OPC_DUP2_X1:
		case OPC_DUP_X1: {
			ir_mode *mode = opcode == OPC_DUP2 ? mode_long : NULL;
			ir_node *top1 = symbolic_pop(mode);
			ir_node *top2 = symbolic_pop(mode);
			symbolic_push(top1);
			symbolic_push(top2);
			symbolic_push(top1);
			continue;
		}
		case OPC_DUP2_X2:
		case OPC_DUP_X2: {
			ir_mode *mode = opcode == OPC_DUP2 ? mode_long : NULL;
			ir_node *top1 = symbolic_pop(mode);
			ir_node *top2 = symbolic_pop(mode);
			ir_node *top3 = symbolic_pop(mode);
			symbolic_push(top1);
			symbolic_push(top3);
			symbolic_push(top2);
			symbolic_push(top1);
			continue;
		}
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

		case OPC_ISHL:  construct_arith(mode_int,    new_Shl);        continue;
		case OPC_LSHL:  construct_arith(mode_long,   new_Shl);        continue;
		case OPC_ISHR:  construct_arith(mode_int,    new_Shrs);       continue;
		case OPC_LSHR:  construct_arith(mode_long,   new_Shrs);       continue;
		case OPC_IUSHR: construct_arith(mode_int,    new_Shr);        continue;
		case OPC_LUSHR: construct_arith(mode_long,   new_Shr);        continue;
		case OPC_IAND:  construct_arith(mode_int,    new_And);        continue;
		case OPC_LAND:  construct_arith(mode_long,   new_And);        continue;
		case OPC_IOR:   construct_arith(mode_int,    new_Or);         continue;
		case OPC_LOR:   construct_arith(mode_long,   new_Or);         continue;
		case OPC_IXOR:  construct_arith(mode_int,    new_Eor);        continue;
		case OPC_LXOR:  construct_arith(mode_long,   new_Eor);        continue;

		case OPC_IINC: {
			uint8_t  index = code->code[i++];
			int8_t   cnst  = (int8_t) code->code[i++];
			ir_node *val   = get_local(index, mode_int);
			ir_node *cnode = new_Const_long(mode_int, cnst);
			ir_node *add   = new_Add(val, cnode, mode_int);
			set_local(index, add);
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
			index += i-3;

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

		case OPC_ACMPEQ:
		case OPC_ACMPNE:
		case OPC_IFNULL:
		case OPC_IFNONNULL: {
			uint8_t  b1    = code->code[i++];
			uint8_t  b2    = code->code[i++];
			uint16_t index = (b1 << 8) | b2;
			index += i-3;

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

			set_cur_block(NULL);
			continue;
		}

		case OPC_IRETURN: construct_vreturn(method_type, mode_int); continue;
		case OPC_LRETURN: construct_vreturn(method_type, mode_long); continue;
		case OPC_FRETURN: construct_vreturn(method_type, mode_float); continue;
		case OPC_DRETURN: construct_vreturn(method_type, mode_double); continue;
		case OPC_ARETURN: construct_vreturn(method_type, mode_reference); continue;
		case OPC_RETURN:  construct_vreturn(method_type, NULL);     continue;

		case OPC_INVOKEVIRTUAL:
		case OPC_INVOKESTATIC: {
			uint8_t    b1     = code->code[i++];
			uint8_t    b2     = code->code[i++];
			uint16_t   index  = (b1 << 8) | b2;
			ir_entity *entity = get_method_entity(index);
			ir_node   *callee = create_symconst(entity);
			ir_type   *type   = get_entity_type(entity);
			unsigned   n_args = get_method_n_params(type);
			ir_node   *args[n_args];

			for (int i = n_args-1; i >= 0; --i) {
				ir_type *arg_type = get_method_param_type(type, i);
				ir_mode *mode     = get_type_mode(arg_type);
				args[i]           = symbolic_pop(mode);
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
				symbolic_push(res);
			}
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

		case OPC_NEW: {
			uint8_t   b1        = code->code[i++];
			uint8_t   b2        = code->code[i++];
			uint16_t  index     = (b1 << 8) | b2;
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

		case OPC_GETSTATIC:
		case OPC_GETFIELD:
			panic("Unimplemented opcode 0x%X found\n", opcode);
		}

		panic("Unknown opcode 0x%X found\n", opcode);
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

	if (method->access_flags & ACCESS_FLAG_NATIVE)
		set_entity_visibility(entity, ir_visibility_external);
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
		type = get_class_type(classname);
		classref->base.link = type;
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
