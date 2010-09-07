#ifndef GCJ_INTERFACE_H
#define GCJ_INTERFACE_H

#include <libfirm/firm.h>
#include "mangle.h"

void     gcji_init(void);
int      gcji_is_api_class(ir_type *type);
void     gcji_class_init(ir_type *type, ir_graph *irg, ir_node *block, ir_node **mem);
ir_node *gcji_allocate_object(ir_type *type, ir_graph *irg, ir_node *block, ir_node **mem);
ir_node *gcji_new_string(ir_entity *bytes, ir_graph *irg, ir_node *block, ir_node **mem);

#endif
