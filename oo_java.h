#ifndef OO_JAVA_H
#define OO_JAVA_H

#include <liboo/oo.h>
#include "class_file.h"

typedef struct {
	oo_type_info  base;
	class_t      *class_info;
} bc2firm_type_info;

typedef struct {
	oo_entity_info base;
	union {
		method_t *method_info;
		field_t  *field_info;
	} member_info;
} bc2firm_entity_info;

void                 oo_java_init(void);
void                 oo_java_deinit(void);
bc2firm_type_info   *create_class_info(class_t* javaclass);
bc2firm_entity_info *create_method_info(method_t* javamethod, class_t* owner);
bc2firm_entity_info *create_field_info(field_t* javafield, class_t* owner);

#define get_class_info_class_t(klass)    ((bc2firm_type_info*)   get_class_info(klass))->class_info
#define get_entity_info_method_t(method) ((bc2firm_entity_info*) get_entity_info(method))->member_info.method_info
#define get_entity_info_field_t(field)   ((bc2firm_entity_info*) get_entity_info(field))->member_info.field_info

#endif
