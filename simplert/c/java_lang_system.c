#include "types.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>

void _ZN4java4lang6System9arraycopyEJvPNS0_6ObjectEiS3_ii(jobject src,
	jint srcpos, jobject dst, jint dstpos, jint length)
{
	// TODO check that src+dest are actualle an array and of the same type
	assert(src->vptr == dst->vptr);
	jarray srca = (jarray)src;
	jarray dsta = (jarray)dst;
	jint src_len = srca->length;
	jint dst_len = dsta->length;
	assert(srcpos + length <= src_len);
	assert(dstpos + length <= dst_len);

	// TODO: how do I get the element size?
	jint element_size = src->vptr->rtti->me.element_type->size_in_bytes;
	assert(element_size > 0);

	memmove((char*)get_array_data(dsta) + (dstpos * element_size),
	        (char*)get_array_data(srca) + (srcpos * element_size),
	        length * element_size);
}
