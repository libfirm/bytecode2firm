#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

typedef int8_t  jbyte;
typedef int16_t jshort;
typedef int32_t jint;
typedef _Bool   jboolean;
typedef float   jfloat;
typedef double  jdouble;

typedef struct java_lang_Object java_lang_Object;
typedef struct java_lang_Class java_lang_Class;
typedef java_lang_Object *jobject;
typedef java_lang_Class *jclass;

typedef struct utf8_const {
	uint16_t hash;
	uint16_t len;
	char     data[];
} utf8_const;

struct java_lang_Object {
	jclass vptr;
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
	utf8_const *name;
	jclass      type;
	uint16_t    flags;
	uint16_t    bsize;
	union {
		int   offset;
		char *addr;
	} u;
} jv_field;

struct java_lang_Class {
	java_lang_Object super;
	jclass           next_or_version;
	utf8_const      *name;
	uint16_t         accflags;
	jclass           superclass;
	jv_constants     constants;
	jv_method       *methods;
	int16_t          method_count;
	int16_t          vtable_method_count;
	jv_field        *fields;
	int              size_in_bytes;
	int16_t          field_count;
	// ...
};

#endif
