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
 * @date    16.03.2007
 * @brief   a pointer to pointer map with a custom compare/hash functions
 * @author  Matthias Braun
 * @version $Id$
 */
#ifndef FIRM_ADT_CPMAP_H
#define FIRM_ADT_CPMAP_H

/**
 * The type of a cpmap compare function.
 *
 * @param p1   pointer to an element
 * @param p2   pointer to another element
 *
 * @return  1 if the elements are identically, zero else
 */
typedef int (*cpmap_cmp_function) (const void *p1, const void *p2);

/**
 * The type of a cpmap hash function.
 */
typedef unsigned (*cpmap_hash_function) (const void *obj);

typedef struct cpmap_entry_t {
	const void *key;
	void       *data;
} cpmap_entry_t;

#define HashSet          cpmap_t
#define HashSetIterator  cpmap_iterator_t
#define HashSetEntry     cpmap_hashset_entry_t
#define ValueType        cpmap_entry_t
#define ADDITIONAL_DATA  cpmap_cmp_function cmp_function; cpmap_hash_function hash_function;
#include "hashset.h"
#undef ADDITIONAL_DATA
#undef ValueType
#undef HashSetEntry
#undef HashSetIterator
#undef HashSet

typedef struct cpmap_t          cpmap_t;
typedef struct cpmap_iterator_t cpmap_iterator_t;

/**
 * Initializes a cpmap
 *
 * @param cpmap           Pointer to allocated space for the cpmap
 * @param hash_function   The hash function to use
 * @param cmp_function    The compare function to use
 */
void cpmap_init(cpmap_t *cpmap, cpmap_hash_function hash_function,
                cpmap_cmp_function cmp_function);

/**
 * Initializes a cpmap
 *
 * @param cpmap              Pointer to allocated space for the cpmap
 * @param hash_function      The hash function to use
 * @param cmp_function       The compare function to use
 * @param expected_elements  Number of elements expected in the cpmap (roughly)
 */
void cpmap_init_size(cpmap_t *cpmap, cpmap_hash_function hash_function,
                     cpmap_cmp_function cmp_function,
                     size_t expected_elements);

/**
 * Destroys a cpmap and frees the memory allocated for hashtable. The memory of
 * the cpmap itself is not freed.
 *
 * @param cpmap   Pointer to the cpmap
 */
void cpmap_destroy(cpmap_t *cpmap);

/**
 * Inserts an element into a cpmap.
 *
 * @param cpmap   Pointer to the cpmap
 * @param key     key under which we file the data
 * @param obj     the data (we just store a pointer to it)
 * @returns       The element itself or a pointer to an existing element
 */
void cpmap_set(cpmap_t *cpmap, const void *key, void *data);

/**
 * Removes an element from a cpmap. Does nothing if the cpmap doesn't contain
 * the element.
 *
 * @param cpmap   Pointer to the cpmap
 * @param key     key of the data to remove
 */
void cpmap_remove(cpmap_t *cpmap, const void *key);

/**
 * Tests whether a cpmap contains a pointer
 *
 * @param cpmap   Pointer to the cpmap
 * @param key     Key of the data to find
 * @returns       The data or NULL if not found
 */
void *cpmap_find(const cpmap_t *cpmap, const void *key);

/**
 * Returns the number of pointers contained in the cpmap
 *
 * @param cpmap   Pointer to the cpmap
 * @returns       Number of pointers contained in the cpmap
 */
size_t cpmap_size(const cpmap_t *cpmap);

/**
 * Initializes a cpmap iterator. Sets the iterator before the first element in
 * the cpmap.
 *
 * @param iterator   Pointer to already allocated iterator memory
 * @param cpmap       Pointer to the cpmap
 */
void cpmap_iterator_init(cpmap_iterator_t *iterator, const cpmap_t *cpmap);

/**
 * Advances the iterator and returns the current element or NULL if all elements
 * in the cpmap have been processed.
 * @attention It is not allowed to use cpmap_set or cpmap_remove while
 *            iterating over a cpmap.
 *
 * @param iterator  Pointer to the cpmap iterator.
 * @returns         Next element in the cpmap or NULL
 */
cpmap_entry_t cpmap_iterator_next(cpmap_iterator_t *iterator);

/**
 * Removed the element the iterator currently points to
 *
 * @param cpmap     Pointer to the cpmap
 * @param iterator  Pointer to the cpmap iterator.
 */
void cpmap_remove_iterator(cpmap_t *cpmap, const cpmap_iterator_t *iterator);

#endif
