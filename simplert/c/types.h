#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

typedef int8_t  jbyte;
typedef int16_t jshort;
typedef int32_t jint;
typedef _Bool   jboolean;
typedef float   jfloat;
typedef double  jdouble;

typedef struct jobject_data jobject_data;
typedef struct jclass_data  jclass_data;
typedef jobject_data *jobject;
typedef jclass_data  *jclass;

typedef struct utf8_const {
	uint16_t hash;
	uint16_t len;
	char     data[];
} utf8_const;

struct jobject_data {
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

struct jclass_data {
	jobject_data super;
	jclass       next_or_version;
	utf8_const  *name;
	uint16_t     accflags;
	jclass       superclass;
	jv_constants constants;
	jv_method   *methods;
	uint16_t     method_count;
	// ...
};

#endif
