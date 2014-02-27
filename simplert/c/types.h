#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

typedef int8_t  jbyte;
typedef int16_t jshort;
typedef uint16_t jchar;
typedef int32_t jint;
typedef _Bool   jboolean;
typedef float   jfloat;
typedef double  jdouble;

typedef struct java_lang_Object java_lang_Object;
typedef struct java_lang_Class java_lang_Class;
typedef java_lang_Object *jobject;
typedef void *jarray; /* TODO */

struct vtable_t;
typedef struct vtable_t vtable_t;

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
	jv_method       *methods;
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
	java_lang_Class *interfaces;
	void            *loader;
	jshort           interface_count;
	jbyte            state;
	void            *thread;
	// ...
};

void init_prim_rtti();
jobject _Jv_AllocObjectNoFinalizer(java_lang_Class *type);
jv_method *get_method(java_lang_Class *cls, const utf8_const *name,
                      const utf8_const *signature);

#endif
