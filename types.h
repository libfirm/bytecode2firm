#ifndef TYPES_H
#define TYPES_H

#include <libfirm/firm.h>

extern ir_type *type_byte;
extern ir_type *type_char;
extern ir_type *type_short;
extern ir_type *type_int;
extern ir_type *type_long;
extern ir_type *type_boolean;
extern ir_type *type_float;
extern ir_type *type_double;
extern ir_type *type_reference;
extern ir_type *type_array_byte_boolean;
extern ir_type *type_array_char;
extern ir_type *type_array_short;
extern ir_type *type_array_int;
extern ir_type *type_array_long;
extern ir_type *type_array_float;
extern ir_type *type_array_double;
extern ir_type *type_array_reference;

extern ir_mode *mode_byte;
extern ir_mode *mode_char;
extern ir_mode *mode_short;
extern ir_mode *mode_int;
extern ir_mode *mode_long;
extern ir_mode *mode_float;
extern ir_mode *mode_double;
extern ir_mode *mode_reference;

extern ident     *vptr_ident;
extern ir_entity *builtin_arraylength;

#endif
