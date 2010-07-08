#include "class_file.h"

#include <stdio.h>
#include <assert.h>

#include "adt/obst.h"
#include "adt/error.h"

static FILE           *in;
static struct obstack  obst;
static class_t        *class_file;
static const char     *classpath;

static uint8_t read_u8(void)
{
	return fgetc(in);
}

static uint16_t read_u16(void)
{
	uint8_t b1 = read_u8();
	uint8_t b2 = read_u8();
	return (b1 << 8) | b2;
}

static uint32_t read_u32(void)
{
	uint8_t b1 = read_u8();
	uint8_t b2 = read_u8();
	uint8_t b3 = read_u8();
	uint8_t b4 = read_u8();
	return (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
}

static void * __attribute__((malloc)) allocate_zero(size_t size)
{
	void *result = obstack_alloc(&obst, size);
	memset(result, 0, size);
	return result;
}

static constant_utf8_string_t *read_constant_utf8_string(void)
{
	constant_utf8_string_t head;
	memset(&head, 0, sizeof(head));
	head.base.kind = CONSTANT_UTF8_STRING;
	head.length    = read_u16();
	
	assert(obstack_object_size(&obst) == 0);
	size_t size = (char*) &head.bytes - (char*) &head;
	obstack_grow(&obst, &head, size);
	for (size_t i = 0; i < (size_t) head.length; ++i) {
		obstack_1grow(&obst, read_u8());
	}
	/* append a 0 byte for convenience */
	obstack_1grow(&obst, '\0');

	return (constant_utf8_string_t*) obstack_finish(&obst);
}

static constant_integer_t *read_constant_integer(void)
{
	constant_integer_t *result = allocate_zero(sizeof(*result));
	result->base.kind = CONSTANT_INTEGER;
	result->value     = read_u32();
	return result;
}

static constant_float_t *read_constant_float(void)
{
	constant_float_t *result = allocate_zero(sizeof(*result));
	result->base.kind = CONSTANT_FLOAT;
	result->value     = read_u32();
	return result;
}

static constant_long_t *read_constant_long(void)
{
	constant_long_t *result = allocate_zero(sizeof(*result));
	result->base.kind  = CONSTANT_LONG;
	result->high_bytes = read_u32();
	result->low_bytes  = read_u32();
	return result;
}

static constant_double_t *read_constant_double(void)
{
	constant_double_t *result = allocate_zero(sizeof(*result));
	result->base.kind  = CONSTANT_DOUBLE;
	result->high_bytes = read_u32();
	result->low_bytes  = read_u32();
	return result;
}

static constant_classref_t *read_constant_classref(void)
{
	constant_classref_t *result = allocate_zero(sizeof(*result));
	result->base.kind  = CONSTANT_CLASSREF;
	result->name_index = read_u16();
	return result;
}

static constant_string_t *read_constant_string(void)
{
	constant_string_t *result = allocate_zero(sizeof(*result));
	result->base.kind    = CONSTANT_STRING;
	result->string_index = read_u16();
	return result;
}

static constant_fieldref_t *read_constant_fieldref(void)
{
	constant_fieldref_t *result = allocate_zero(sizeof(*result));
	result->base.kind           = CONSTANT_FIELDREF;
	result->class_index         = read_u16();
	result->name_and_type_index = read_u16();
	return result;
}

static constant_methodref_t *read_constant_methodref(void)
{
	constant_methodref_t *result = allocate_zero(sizeof(*result));
	result->base.kind           = CONSTANT_METHODREF;
	result->class_index         = read_u16();
	result->name_and_type_index = read_u16();
	return result;
}

static constant_interfacemethodref_t *read_constant_interfacemethodref(void)
{
	constant_interfacemethodref_t *result = allocate_zero(sizeof(*result));
	result->base.kind           = CONSTANT_INTERFACEMETHODREF;
	result->class_index         = read_u16();
	result->name_and_type_index = read_u16();
	return result;
}

static constant_name_and_type_t *read_constant_name_and_type(void)
{
	constant_name_and_type_t *result = allocate_zero(sizeof(*result));
	result->base.kind        = CONSTANT_NAMEANDTYPE;
	result->name_index       = read_u16();
	result->descriptor_index = read_u16();
	return result;
}

static constant_t *read_constant(void)
{
	constant_kind_t kind = (constant_kind_t) read_u8();
	switch (kind) {
	case CONSTANT_UTF8_STRING:        return (constant_t*) read_constant_utf8_string();
	case CONSTANT_INTEGER:            return (constant_t*) read_constant_integer();
	case CONSTANT_FLOAT:              return (constant_t*) read_constant_float();
	case CONSTANT_LONG:               return (constant_t*) read_constant_long();
	case CONSTANT_DOUBLE:             return (constant_t*) read_constant_double();
	case CONSTANT_CLASSREF:           return (constant_t*) read_constant_classref();
	case CONSTANT_STRING:             return (constant_t*) read_constant_string();
	case CONSTANT_FIELDREF:           return (constant_t*) read_constant_fieldref();
	case CONSTANT_METHODREF:          return (constant_t*) read_constant_methodref();
	case CONSTANT_INTERFACEMETHODREF: return (constant_t*) read_constant_interfacemethodref();
	case CONSTANT_NAMEANDTYPE:        return (constant_t*) read_constant_name_and_type();
	}
	panic("Unknown constant type %d in classfile", kind);
}



static attribute_t *read_attribute(void);

static attribute_unknown_t *read_attribute_unknown(uint16_t name_index)
{
	attribute_unknown_t head;
	head.base.kind  = ATTRIBUTE_CUSTOM;
	head.name_index = name_index;
	head.length     = read_u32();

	assert(obstack_object_size(&obst) == 0);
	obstack_grow(&obst, &head, sizeof(head));
	for (size_t i = 0; i < (size_t) head.length; ++i) {
		obstack_1grow(&obst, read_u8());
	}

	return obstack_finish(&obst);
}

static attribute_code_t *read_attribute_code(void)
{
	uint32_t length = read_u32();

	attribute_code_t *code = allocate_zero(sizeof(*code));
	code->base.kind   = ATTRIBUTE_CODE;
	code->max_stack   = read_u16();
	code->max_locals  = read_u16();
	code->code_length = read_u32();
	code->code        = obstack_alloc(&obst, code->code_length);
	for (size_t i = 0; i < (size_t) code->code_length; ++i) {
		code->code[i] = read_u8();
	}

	code->n_exceptions = read_u16();
	code->exceptions   = obstack_alloc(&obst,
			code->n_exceptions * sizeof(code->exceptions[0]));
	for (size_t i = 0; i < (size_t) code->n_exceptions; ++i) {
		exception_t *exception = &code->exceptions[i];
		exception->start_pc   = read_u16();
		exception->end_pc     = read_u16();
		exception->handler_pc = read_u16();
		exception->catch_type = read_u16();
	}

	code->n_attributes = read_u16();
	code->attributes = obstack_alloc(&obst,
			code->n_attributes * sizeof(code->attributes[0]));
	for (size_t i = 0; i < (size_t) code->n_attributes; ++i) {
		code->attributes[i] = read_attribute();
	}

	/* TODO make this check more exact */
	assert(code->code_length < length);

	return code;
}

static attribute_t *read_attribute(void)
{
	uint16_t    name_index    = read_u16();
	constant_t *name_constant = class_file->constants[name_index];
	const char *name          = name_constant->utf8_string.bytes;

	if (strcmp(name, "Code") == 0) {
		return (attribute_t*) read_attribute_code();
	} else {
		return (attribute_t*) read_attribute_unknown(name_index);
	}
}



static field_t *read_field(void)
{
	field_t *field = allocate_zero(sizeof(*field));
	field->access_flags     = read_u16();
	field->name_index       = read_u16();
	field->descriptor_index = read_u16();
	field->n_attributes     = read_u16();
	field->attributes       = obstack_alloc(&obst,
			field->n_attributes * sizeof(field->attributes[0]));
	for (size_t i = 0; i < (size_t) field->n_attributes; ++i) {
		field->attributes[i] = read_attribute();
	}

	return field;
}

static method_t *read_method(void)
{
	method_t *method = allocate_zero(sizeof(*method));
	method->access_flags     = read_u16();
	method->name_index       = read_u16();
	method->descriptor_index = read_u16();
	method->n_attributes     = read_u16();
	method->attributes       = obstack_alloc(&obst,
			method->n_attributes * sizeof(method->attributes[0]));
	for (size_t i = 0; i < (size_t) method->n_attributes; ++i) {
		method->attributes[i] = read_attribute();
	}

	return method;
}

class_t *read_class_file(void)
{
	uint32_t magic = read_u32();
	if (magic != 0xCAFEBABE) {
		panic("Not a class file");
	}
	uint16_t minor_version = read_u16();
	uint16_t major_version = read_u16();
	assert((major_version == 49 || major_version == 50) && minor_version == 0);

	class_file = allocate_zero(sizeof(*class_file));
	class_file->n_constants = read_u16();
	class_file->constants   = obstack_alloc(&obst,
			class_file->n_constants * sizeof(class_file->constants[0]));
	class_file->constants[0] = NULL;
	for (size_t i = 1; i < (size_t) class_file->n_constants; ++i) {
		constant_t *constant = read_constant();
		class_file->constants[i] = constant;
		/* long+double takes up 2 slots (the 2nd slot is considered unusable) */
		if (constant->kind == CONSTANT_LONG 
				|| constant->kind == CONSTANT_DOUBLE) {
			class_file->constants[i+1] = NULL;
			++i;
		}
	}

	class_file->access_flags = read_u16();
	class_file->this_class   = read_u16();
	class_file->super_class  = read_u16();

	class_file->n_interfaces = read_u16();
	class_file->interfaces   = obstack_alloc(&obst,
			class_file->n_interfaces * sizeof(class_file->interfaces[0]));
	for (size_t i = 0; i < (size_t) class_file->n_interfaces; ++i) {
		class_file->interfaces[i] = read_u16();
	}

	class_file->n_fields = read_u16();
	class_file->fields   = obstack_alloc(&obst,
			class_file->n_fields * sizeof(class_file->fields[0]));
	for (size_t i = 0; i < (size_t) class_file->n_fields; ++i) {
		class_file->fields[i] = read_field();
	}

	class_file->n_methods = read_u16();
	class_file->methods   = obstack_alloc(&obst,
			class_file->n_methods * sizeof(class_file->methods[0]));
	for (size_t i = 0; i < (size_t) class_file->n_methods; ++i) {
		class_file->methods[i] = read_method();
	}

	class_file->n_attributes = read_u16();
	class_file->attributes   = obstack_alloc(&obst,
			class_file->n_attributes * sizeof(class_file->attributes[0]));
	for (size_t i = 0; i < (size_t) class_file->n_attributes; ++i) {
		class_file->attributes[i] = read_attribute();
	}

	return class_file;
}

class_t *read_class(const char *classname)
{
	assert(obstack_object_size(&obst) == 0);
	unsigned len = obstack_printf(&obst, "%s%s.class", classpath, classname);
	char *classfilename = obstack_finish(&obst);

	assert (len == 8 + strlen(classname) + 6);
	if (len != strlen(classfilename)) classfilename[len] = '\0'; // FIXME: remove!

	in = fopen(classfilename, "r");
	if (in == NULL) {
		panic("Couldn't find class '%s' (%s)\n", classname, classfilename);
	}

	class_file = read_class_file();
	fclose(in);

	return class_file;
}

void class_file_init(const char *new_classpath)
{
	obstack_init(&obst);

	size_t  len = strlen(new_classpath) + 1;
	obstack_grow(&obst, new_classpath, len);
	classpath   = obstack_finish(&obst);
}

void class_file_exit(void)
{
	obstack_free(&obst, NULL);
}
