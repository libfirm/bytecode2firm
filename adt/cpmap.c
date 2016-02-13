/*
 * Copyright (C) 1995-2008 University of Karlsruhe.  All right reserved.
 *
 * This file is part of libFirm.
 *
 * This file may be distributed and/or modified under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation and appearing in the file LICENSE.GPL included in the
 * packaging of this file.
 *
 * Licensees holding valid libFirm Professional Edition licenses may use
 * this file in accordance with the libFirm Commercial License.
 * Agreement provided with the Software.
 *
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE.
 */

/**
 * @file
 * @brief   Custom pointer set
 * @author  Matthias Braun
 * @version $Id: cpmap.c 17143 2008-01-02 20:56:33Z beck $
 *
 * This implements a set of pointers which allows to specify custom callbacks
 * for comparing and hashing it's elements.
 */
#include "cpmap.h"

#define HashSet                   cpmap_t
#define HashSetIterator           cpmap_iterator_t
#define HashSetEntry              cpmap_hashset_entry_t
#define ValueType                 cpmap_entry_t
#define NullValue                 (cpmap_entry_t) { NULL, NULL }
#define DeletedValue              (cpmap_entry_t) { ((void*)-1), NULL }
#define KeyType                   const void*
#define ConstKeyType              KeyType
#define GetKey(value)             (value).key
#define InitData(self,value,k)    (value).key = (k)
#define Hash(this,key)            this->hash_function(key)
#define KeysEqual(this,key1,key2) this->cmp_function(key1, key2)
#define SetRangeEmpty(ptr,size)   memset(ptr, 0, (size) * sizeof(HashSetEntry))
#define EntryIsEmpty(entry)       (EntryGetValue(entry).key == NULL)
#define EntryIsDeleted(entry)     (EntryGetValue(entry).key == ((void*)-1))

void cpmap_init_(cpmap_t *map);
#define hashset_init            cpmap_init_
void cpmap_init_size_(cpmap_t *map, size_t size);
#define hashset_init_size       cpmap_init_size_
#define hashset_destroy         cpmap_destroy
cpmap_entry_t *cpmap_insert_(cpmap_t *map, const void *key);
#define hashset_insert          cpmap_insert_
#define hashset_remove          cpmap_remove
cpmap_entry_t *cpmap_find_(const cpmap_t *map, const void *key);
#define hashset_find            cpmap_find_
#define hashset_size            cpmap_size
#define hashset_iterator_init   cpmap_iterator_init
#define hashset_iterator_next   cpmap_iterator_next
#define hashset_remove_iterator cpmap_remove_iterator

#include "hashset.c.h"

void cpmap_init(cpmap_t *map, cpmap_hash_function hash_function,
                cpmap_cmp_function cmp_function)
{
	map->hash_function = hash_function;
	map->cmp_function = cmp_function;
	cpmap_init_(map);
}

void cpmap_init_size(cpmap_t *map, cpmap_hash_function hash_function,
                     cpmap_cmp_function cmp_function, size_t expected_elems)
{
	map->hash_function = hash_function;
	map->cmp_function = cmp_function;
	cpmap_init_size_(map, expected_elems);
}

void cpmap_set(cpmap_t *map, const void *key, void *data)
{
	if (data == NULL) {
		cpmap_remove(map, key);
	} else {
		cpmap_entry_t *entry = cpmap_insert_(map, key);
		entry->data = data;
	}
}

void *cpmap_find(const cpmap_t *map, const void *key)
{
	cpmap_entry_t *entry = cpmap_find_(map, key);
	if (entry == NULL)
		return NULL;
	return entry->data;
}

