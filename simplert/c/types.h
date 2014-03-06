#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef int8_t  jbyte;
typedef int16_t jshort;
typedef uint16_t jchar;
typedef int32_t jint;
typedef int64_t jlong;
typedef uint8_t jboolean;
typedef float   jfloat;
typedef double  jdouble;
typedef jint    jsize;

typedef struct java_lang_Object java_lang_Object;
typedef struct java_lang_Class java_lang_Class;
typedef java_lang_Object      *jobject;
typedef struct array_header_t *jarray;

struct vtable_t {
	java_lang_Class *rtti;
	void            *gc_descr;
	void            *dynamic_methods[];
};
typedef struct vtable_t vtable_t;

typedef struct extended_vtable_t {
	uint32_t x0;
	uint32_t x1;
	vtable_t vtable;
} extended_vtable_t;

typedef struct utf8_const {
	uint16_t hash;
	uint16_t len;
	char     data[];
} utf8_const;

struct java_lang_Object {
	vtable_t *vptr;
};

typedef struct jv_constants {
	jint   size;
	jbyte *tags;
	void  *data;
} jv_constants;

typedef struct jv_method {
	utf8_const  *name;
	utf8_const  *signature;
	uint16_t     accflags;
	uint16_t     index;
	void        *code;
	utf8_const **throws;
} jv_method;

typedef struct jv_field {
	utf8_const      *name;
	java_lang_Class *type;
	uint16_t         flags;
	uint16_t         bsize;
	union {
		int   offset;
		char *addr;
	} u;
} jv_field;

struct java_lang_Class {
	java_lang_Object base;
	java_lang_Class *next_or_version;
	utf8_const      *name;
	uint16_t         accflags;
	java_lang_Class *superclass;
	jv_constants     constants;
	union {
		jv_method       *methods;
		java_lang_Class *element_type;
	} me;
	int16_t          method_count;
	int16_t          vtable_method_count;
	jv_field        *fields;
	int              size_in_bytes;
	int16_t          field_count;
	int16_t          static_field_count;
	vtable_t        *vtable;
	void            *otable;
	void            *otable_syms;
	void            *atable;
	void            *atable_syms;
	void            *itable;
	void            *itable_syms;
	void            *catch_classes;
	java_lang_Class **interfaces;
	void            *loader;
	jshort           interface_count;
	jbyte            state;
	void            *thread;
	jshort           depth;
	java_lang_Class *ancestors;
	void            *idt_ioffsets;
	java_lang_Class *arrayclass;
	// ...
};

typedef struct java_lang_String {
	java_lang_Object  base;
	jchar            *data;
	jint              boffset;
	jint              count;
} java_lang_String;

typedef struct array_header_t {
	java_lang_Object base;
	jint             length;
} array_header_t;

// necessary for C strict aliasing rules
union u0 {
	java_lang_Object o;
	java_lang_String s;
};

// necessary for C strict aliasing rules
union u1 {
	java_lang_Object base;
	java_lang_Class  cls;
};

enum {
	JV_STATE_NOTHING = 0,
	JV_STATE_DONE    = 14
};

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

extern java_lang_Class objarray_class;

void init_prim_rtti();
jobject _Jv_AllocObjectNoFinalizer(java_lang_Class *type);
jv_method *get_method(java_lang_Class *cls, const utf8_const *name,
                      const utf8_const *signature);

jarray _Jv_NewObjectArray(jsize count, java_lang_Class *eltype, jobject init);

java_lang_String *string_from_c_chars(const char *chars, size_t len);
java_lang_String *_Z22_Jv_NewStringUtf8ConstP13_Jv_Utf8Const(const utf8_const *cnst);

jboolean _ZN4java4lang5Class11isPrimitiveEJbv(const java_lang_Class *cls);
jboolean _ZN4java4lang5Class7isArrayEJbv(const java_lang_Class *this_);

java_lang_String *double_to_string(jdouble value, bool is_float);

unsigned calc_string_hash(const char *chars, size_t len);

static inline void *get_array_data(jarray arr)
{
	return ((char*)arr) + sizeof(array_header_t);
}

#endif
