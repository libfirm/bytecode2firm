#ifndef CLASS_FILE_H
#define CLASS_FILE_H

#include <stdint.h>
#include <stdbool.h>
#include <libfirm/firm.h>

typedef enum {
	CONSTANT_UTF8_STRING        = 1,
	CONSTANT_INTEGER            = 3,
	CONSTANT_FLOAT              = 4,
	CONSTANT_LONG               = 5,
	CONSTANT_DOUBLE             = 6,
	CONSTANT_CLASSREF           = 7,
	CONSTANT_STRING             = 8,
	CONSTANT_FIELDREF           = 9,
	CONSTANT_METHODREF          = 10,
	CONSTANT_INTERFACEMETHODREF = 11,
	CONSTANT_NAMEANDTYPE        = 12,
} constant_kind_t;

typedef enum access_flags_t {
	ACCESS_FLAG_PUBLIC          = 1U << 0,
	ACCESS_FLAG_PRIVATE         = 1U << 1,
	ACCESS_FLAG_PROTECTED       = 1U << 2,
	ACCESS_FLAG_STATIC          = 1U << 3,
	ACCESS_FLAG_FINAL           = 1U << 4,
	ACCESS_FLAG_SYNCHRONIZED    = 1U << 5,
	ACCESS_FLAG_VOLATILE        = 1U << 6,
	ACCESS_FLAG_TRANSIENT       = 1U << 7,
	ACCESS_FLAG_NATIVE          = 1U << 8,
	ACCESS_FLAG_INTERFACE       = 1U << 9,
	ACCESS_FLAG_ABSTRACT        = 1U << 10,
	ACCESS_FLAG_STRICT          = 1U << 11
} access_flags_t;

typedef uint16_t constref_t;

typedef struct constant_base_t {
	uint8_t  kind;
	void    *link;
} constant_base_t;

typedef struct constant_utf8_string_t {
	constant_base_t  base;
	uint16_t         length;
	char             bytes[];
} constant_utf8_string_t;

typedef struct constant_integer_t {
	constant_base_t  base;
	uint32_t         value;
} constant_integer_t;

typedef struct constant_float_t {
	constant_base_t  base;
	uint32_t         value;
} constant_float_t;

typedef struct constant_long_t {
	constant_base_t  base;
	uint32_t         high_bytes;
	uint32_t         low_bytes;
} constant_long_t;

typedef struct constant_double_t {
	constant_base_t  base;
	uint32_t         high_bytes;
	uint32_t         low_bytes;
} constant_double_t;

typedef struct constant_classref_t {
	constant_base_t  base;
	uint16_t         name_index;
} constant_classref_t;

typedef struct constant_string_t {
	constant_base_t  base;
	uint16_t         string_index;
} constant_string_t;

typedef struct constant_fieldref_t {
	constant_base_t  base;
	uint16_t         class_index;
	uint16_t         name_and_type_index;
} constant_fieldref_t;

typedef struct constant_methodref_t {
	constant_base_t  base;
	uint16_t         class_index;
	uint16_t         name_and_type_index;
} constant_methodref_t;

typedef struct constant_interfacemethodref_t {
	constant_base_t  base;
	uint16_t         class_index;
	uint16_t         name_and_type_index;
} constant_interfacemethodref_t;

typedef struct constant_name_and_type_t {
	constant_base_t  base;
	uint16_t         name_index;
	uint16_t         descriptor_index;
} constant_name_and_type_t;

typedef union constant_t {
	uint8_t                        kind;
	constant_base_t                base;
	constant_utf8_string_t         utf8_string;
	constant_integer_t             integer;
	constant_float_t               floatc;
	constant_long_t                longc;
	constant_double_t              doublec;
	constant_classref_t            classref;
	constant_string_t              string;
	constant_fieldref_t            fieldref;
	constant_methodref_t           methodref;
	constant_interfacemethodref_t  interfacemethodref;
	constant_name_and_type_t       name_and_type;
} constant_t;




typedef enum attribute_kind_t {
	ATTRIBUTE_CUSTOM,
	ATTRIBUTE_CODE
} attribute_kind_t;

typedef union attribute_t attribute_t;

typedef struct attribute_base_t {
	uint8_t  kind;
} attribute_base_t;

typedef struct attribute_unknown_t {
	attribute_base_t base;
	uint16_t         name_index;
	uint32_t         length;
	uint8_t          data[];
} attribute_unknown_t;

typedef struct exception_t {
	uint16_t  start_pc;
	uint16_t  end_pc;
	uint16_t  handler_pc;
	uint16_t  catch_type;
} exception_t;

typedef struct attribute_code_t {
	attribute_base_t base;
	uint16_t         max_stack;
	uint16_t         max_locals;
	uint32_t         code_length;
	uint8_t         *code;
	uint16_t         n_exceptions;
	exception_t     *exceptions;
	uint16_t         n_attributes;
	attribute_t    **attributes;
} attribute_code_t;

union attribute_t {
	uint8_t             kind;
	attribute_base_t    base;
	attribute_unknown_t unknown;
	attribute_code_t    code;
};



typedef struct field_t {
	uint16_t      access_flags;
	uint16_t      name_index;
	uint16_t      descriptor_index;
	uint16_t      n_attributes;
	attribute_t **attributes;

	ir_entity    *link;
} field_t;

typedef struct method_t {
	uint16_t      access_flags;
	uint16_t      name_index;
	uint16_t      descriptor_index;
	uint16_t      n_attributes;
	attribute_t **attributes;

	ir_entity    *link;
} method_t;

typedef struct {
	uint16_t      n_constants;
	constant_t  **constants;
	uint16_t      access_flags;
	uint16_t      this_class;
	uint16_t      super_class;
	uint16_t      n_interfaces;
	uint16_t     *interfaces;
	uint16_t      n_fields;
	field_t     **fields;
	uint16_t      n_methods;
	method_t    **methods;
	uint16_t      n_attributes;
	attribute_t **attributes;

	ir_type      *link;
	bool          is_extern;
} class_t;

void class_file_init(const char *classpath, const char *bootclasspath);
void class_file_exit(void);
class_t *read_class_file(void);
class_t *read_class(const char *classname);

#endif
