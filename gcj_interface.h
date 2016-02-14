#ifndef GCJ_INTERFACE_H
#define GCJ_INTERFACE_H

#include <libfirm/firm.h>
#include "class_file.h"

#define GCJI_LENGTH_OFFSET 4
#define GCJI_DATA_OFFSET   8

extern ident *superobject_ident;
extern bool   create_jcr_segment;

void       gcji_init(void);
void       gcji_deinit(void);
void       gcji_class_init(ir_type *type);
ir_node   *gcji_allocate_object(ir_type *type);
ir_node   *gcji_allocate_array(ir_type *eltype, ir_node *count);
ir_entity *gcji_emit_utf8_const(constant_t *constant, int mangle_slash);
ir_node   *gcji_new_string(ir_entity *bytes);
ir_node   *gcji_new_multiarray(ir_node *array_class_ref, unsigned dims,
                               ir_node **sizes);
ir_entity *gcji_get_rtti_entity(ir_type *classtype);
ir_node   *gcji_get_runtime_classinfo(ir_type *type);
void       gcji_create_rtti_entity(ir_type *type);
void       gcji_setup_rtti_entity(class_t *cls, ir_type *type);
ir_node   *gcji_lookup_interface(ir_node *obptr, ir_type *iface, ir_entity *method, ir_graph *irg, ir_node *block, ir_node **mem);
void       gcji_checkcast(ir_type *classtype, ir_node *objptr);
void       gcji_create_vtable_entity(ir_type *type);
void       gcji_set_java_lang_class(ir_type *type);
void       gcji_set_java_lang_object(ir_type *type);
void       gcji_add_java_lang_class_fields(ir_type *type);
void       gcji_create_array_type(void);
ir_entity *gcji_get_abstract_method_entity(void);

void       init_rta_callbacks(void);
void       deinit_rta_callbacks(void);
ir_entity *detect_call(ir_node *call);

#endif
