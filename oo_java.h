#ifndef OO_JAVA_H
#define OO_JAVA_H

#include <liboo/oo.h>
#include <libfirm/firm.h>
#include "class_file.h"

void oo_java_init(void);
void oo_java_deinit(void);

void oo_java_setup_type_info(ir_type *classtype, class_t* javaclass);
void oo_java_setup_method_info(ir_entity* method, method_t* javamethod, ir_type *defining_class, class_t *defining_javaclass);
void oo_java_setup_field_info(ir_entity *field, field_t* javafield, ir_type *defining_class, class_t *defining_javaclass);

#endif
