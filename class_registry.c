#include "class_registry.h"
#include "adt/cpmap.h"
#include "adt/hashptr.h"
#include <string.h>

static cpmap_t class_registry;

static int class_registry_keys_equal(const void *p1, const void *p2)
{
	const char *s1 = (const char *) p1;
	const char *s2 = (const char *) p2;
	return strcmp(s1, s2) == 0;
}

static unsigned class_registry_key_hash(const void *p)
{
	const char *s = (const char *)p;
	return firm_fnv_hash_str(s);
}

void class_registry_init(void)
{
	cpmap_init(&class_registry, class_registry_key_hash,
	           class_registry_keys_equal);
}

ir_type *class_registry_get(const char *classname)
{
	return cpmap_find(&class_registry, classname);
}

void class_registry_set(const char *classname, ir_type *type)
{
	cpmap_set(&class_registry, classname, type);
}
