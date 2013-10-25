#include "mangle.h"

#include <string.h>
#include <assert.h>
#include "liboo/oo.h"
#include "adt/cpset.h"
#include "adt/error.h"

#ifdef __APPLE__
static const char *mangle_prefix = "_";
#else
static const char *mangle_prefix = "";
#endif

static const char* base36 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static char *duplicate_string_n(const char* s, size_t n)
{
	char *new_string = XMALLOCN(char, n+1);
	memcpy(new_string, s, n);
	new_string[n] = '\0';
	return new_string;
}

static char *duplicate_string(const char *s)
{
	size_t len = strlen(s);
	return duplicate_string_n(s, len);
}

// (entity name) substitution table
typedef struct {
	char *name;
	char *mangled;
} st_entry;

static cpset_t st;

static int string_cmp (const void *p1, const void *p2)
{
	st_entry *entry1 = (st_entry*) p1;
	st_entry *entry2 = (st_entry*) p2;
	return strcmp(entry1->name, entry2->name) == 0;
}

static unsigned string_hash (const void *obj)
{
	unsigned hash = 0;
	const char *s = ((st_entry*)obj)->name;
	size_t len = strlen(s);
	for (size_t i = 0; i < len; i++) {
		hash = (31 * hash) + s[i];
	}
	return hash;
}

static void free_ste(st_entry *ste)
{
	if (ste == NULL)
		return;

	free(ste->name);
	free(ste->mangled);
	free(ste);
}

// compression table utility functions
#define COMPRESSION_TABLE_SIZE 36 // theoretically, there could be substitution patterns with more than one digit.

typedef struct {
	char *prefix;
} compression_table_entry_t;

typedef struct {
	compression_table_entry_t entries[COMPRESSION_TABLE_SIZE];
	int                       next_slot;
} compression_table_t;

static void mangle_ct_init(compression_table_t *ct)
{
	memset(ct, 0, sizeof(compression_table_t));
}

static void mangle_ct_flush(compression_table_t *ct)
{
	for (int i = 0; i < COMPRESSION_TABLE_SIZE; i++) {
		if (ct->entries[i].prefix) {
			free(ct->entries[i].prefix);
		}
		ct->entries[i].prefix = NULL;
	}
	ct->next_slot = 0;
}

static int mangle_ct_find(compression_table_t* ct, const char* prefix)
{
	for (int i = 0; i < ct->next_slot; i++) {
		if (strcmp(ct->entries[i].prefix, prefix) == 0) {
			return i;
		}
	}
	return -1;
}
static void mangle_ct_dump(compression_table_t *ct)
{
	fprintf(stderr,"--- Dumping compression table ---\n");
	for (int i = 0; i < ct->next_slot; i++)
		fprintf(stderr, "%d: %s\n", i, ct->entries[i].prefix);
	fprintf(stderr,"---------------------------------\n");
}

static void mangle_ct_insert(compression_table_t *ct, const char* prefix)
{
	assert(ct->next_slot < COMPRESSION_TABLE_SIZE);
	ct->entries[ct->next_slot].prefix = duplicate_string(prefix);
	ct->next_slot++;
	(void) mangle_ct_dump;
}

static void mangle_emit_substitution(int match, struct obstack *obst)
{
	obstack_1grow(obst, 'S');
	if (match > 0)
		obstack_1grow(obst, base36[match-1]);
	obstack_1grow(obst, '_');
}


static void mangle_add_name_substitution(const char *name, const char *mangled)
{
	st_entry *ste = XMALLOC(st_entry);
	ste->name = duplicate_string(name);
	ste->mangled = duplicate_string(mangled);
	st_entry* obj = (st_entry*) cpset_insert(&st, ste);
	/* noone should insert 2 substitutions for the same name */
	if (obj != ste)
		panic("more than 1 substitution for name '%s'\n", name);
}

#define ALL_PRIM_TYPES 'V': case 'Z': case 'B': case 'C': case 'S': case 'I': case 'J': case 'F': case 'D'

static size_t type_desc_len(const char* desc)
{
	switch (*desc) {
	case ALL_PRIM_TYPES:
		return 1;
	case 'L': ;
		const char *p = desc;
		while (*p != ';') p++; p++; // skip the ';'
		return p-desc;
	case '[':
		return type_desc_len(desc+1)+1;
	default:
		return 0;
	}

}

static void mangle_types(const char *desc, struct obstack *obst, compression_table_t *ct);

static bool mangle_qualified_class_name(const char *classname, bool is_pointer, struct obstack *obst, compression_table_t *ct)
{
	size_t      slen           = strlen(classname);
	bool        emitted_N      = false;
	char       *request_buffer = XMALLOCN(char, slen+2);

	if (is_pointer) {
		// load "*classname" (i.e. the pointer version) into request_buffer and check for a match.
		request_buffer[0] = '*';
		strncpy(request_buffer+1, classname, slen);
		request_buffer[slen+1] = '\0';
		int full_match_p = mangle_ct_find(ct, request_buffer);
		if (full_match_p > 0) {
			mangle_emit_substitution(full_match_p, obst);
			free(request_buffer);
			return emitted_N;
		}
	} else {
		strncpy(request_buffer, classname, slen);
		request_buffer[slen] = '\0';
	}

	// check for a full match, which is not considered a composite name.
	int full_match = mangle_ct_find(ct, request_buffer+(is_pointer ? 1 : 0));
	if (full_match > 0) {
		if (is_pointer) {
			obstack_1grow(obst, 'P');
			mangle_ct_insert(ct, request_buffer); // request_buffer still contains "*classname", insert it.
		}
		mangle_emit_substitution(full_match, obst);
		free(request_buffer);
		return emitted_N;
	}

	// no full match, we have to construct a new composite name
	if (is_pointer)
		obstack_1grow(obst, 'P');

	obstack_1grow(obst, 'N');
	emitted_N = true;
	const char *p = classname;

	// search for the longest prefix (component-wise) that is in the ct and use it.
	// New components are emitted as "<length>component" (e.g. "4java2io")
	int last_match = -1;
	while (*p != '\0' && *p != ';') {
		while (*p == '/')
			++p;
		/* search for '/' or '\0' */
		size_t l;
		for (l = 0; p[l] != '\0' && p[l] != ';' && p[l] != '/'; ++l) {
		}

		const char *comp_begin   = p;
		const char *comp_end     = p + l;
		unsigned    comp_end_idx = (comp_end-classname);
		strncpy(request_buffer, classname, comp_end_idx);
		request_buffer[comp_end_idx] = '\0';
		p = comp_end;

		int match = mangle_ct_find(ct, request_buffer);
		if (match >= 0) {
			last_match = match;
		} else {
			mangle_ct_insert(ct, request_buffer);

			if (last_match >= 0) {
				mangle_emit_substitution(last_match, obst);
				last_match = -1;
			}
			obstack_printf(obst, "%d", l);
			obstack_grow(obst, comp_begin, l);
		}
	}

	if (is_pointer) {
		// load "*classname" again and insert it after "classname"
		// (which has been inserted as last step by the construction of the composite name)
		request_buffer[0] = '*';
		strncpy(request_buffer+1, classname, slen);
		request_buffer[slen+1] = '\0';
		mangle_ct_insert(ct, request_buffer);
	}

	free(request_buffer);

	return emitted_N;
}

static void mangle_array_type_for_compression_table(const char *array_desc, struct obstack *obst)
{
	assert(*array_desc == '[');
	obstack_1grow(obst, '<');
	switch (array_desc[1]) { // the character after '['
	case ALL_PRIM_TYPES:
		 obstack_1grow(obst, array_desc[1]);
		break;
	case 'L': ;
		obstack_1grow(obst, '*');
		size_t classname_len = type_desc_len(array_desc+1);
		obstack_grow(obst, array_desc+2, classname_len-2 /*omit L and ;*/);
		break;
	case '[':
		mangle_array_type_for_compression_table(array_desc+1, obst);
		break;
	}
	obstack_1grow(obst, '>');
}

static void mangle_array_type(const char *array_desc, struct obstack *obst, compression_table_t *ct)
{
	struct obstack unsub_obst;
	obstack_init(&unsub_obst);
	obstack_1grow(&unsub_obst, '*');
	mangle_array_type_for_compression_table(array_desc, &unsub_obst);
	obstack_1grow(&unsub_obst, '\0');
	const char *unsubstituted = obstack_finish(&unsub_obst);

	int full_match = mangle_ct_find(ct, unsubstituted);
	if (full_match >= 0) {
		mangle_emit_substitution(full_match, obst);
	} else {
		obstack_1grow(obst, 'P');

		int jarray_match = mangle_ct_find(ct, "JArray");
		if (jarray_match >= 0) {
			mangle_emit_substitution(jarray_match, obst);
		} else {
			obstack_grow(obst, "6JArray", 7);
			mangle_ct_insert(ct, "JArray");
		}
		obstack_1grow(obst, 'I');

		mangle_types(array_desc + 1, obst, ct);

		obstack_1grow(obst, 'E');

		mangle_ct_insert(ct, unsubstituted+1); // insert the non-pointer version of the JArray.
		mangle_ct_insert(ct, unsubstituted);
	}
	obstack_free(&unsub_obst, NULL);
}

static void mangle_types(const char *desc, struct obstack *obst, compression_table_t *ct)
{
	size_t desc_len = strlen(desc);
	unsigned i = 0;
	while (i < desc_len) {
		const char *cp = desc+i;
		size_t cur_len = type_desc_len(cp);

		switch (desc[i]) {
		case 'V': obstack_1grow(obst, 'v'); break;
		case 'Z': obstack_1grow(obst, 'b'); break;
		case 'B': obstack_1grow(obst, 'c'); break;
		case 'C': obstack_1grow(obst, 'w'); break;
		case 'S': obstack_1grow(obst, 's'); break;
		case 'I': obstack_1grow(obst, 'i'); break;
		case 'J': obstack_1grow(obst, 'x'); break;
		case 'F': obstack_1grow(obst, 'f'); break;
		case 'D': obstack_1grow(obst, 'd'); break;
		case 'L': ;
			const char *classname = duplicate_string_n(cp+1, cur_len-2); // omit L and ;
			bool emitted_N = mangle_qualified_class_name(classname, true, obst, ct);
			if (emitted_N)
				obstack_1grow(obst, 'E');
			free((char*)classname);
			break;
		case '[': ;
			const char *array_desc = duplicate_string_n(cp, cur_len); // don't omit '['
			mangle_array_type(array_desc, obst, ct);
			free((char*)array_desc);
			break;
		default:
			panic("Invalid type signature");
		}

		i += cur_len;
	}
}

ident *mangle_member_name(const char *defining_class, const char *member_name, const char *member_signature)
{
	struct obstack mobst;
	obstack_init(&mobst);

	compression_table_t ct;
	mangle_ct_init(&ct);

	obstack_grow(&mobst, mangle_prefix, strlen(mangle_prefix));
	obstack_grow(&mobst, "_Z", 2);

	mangle_qualified_class_name(defining_class, false, &mobst, &ct);

	st_entry ste;
	ste.name = (char*)member_name;
	st_entry *found_ste = cpset_find(&st, &ste);
	if (found_ste == NULL) {
		size_t len = strlen(member_name);
		obstack_printf(&mobst, "%d%s", (int) len, member_name);
	} else {
		obstack_printf(&mobst, "%s", found_ste->mangled);
	}

	obstack_1grow(&mobst, 'E');

	if (member_signature == NULL)
		goto name_finished;

	assert(*member_signature == '(');
	const char *params_begin = member_signature + 1; // skip '('
	const char *params_end   = params_begin;
	while (*params_end != ')') params_end++;
	size_t      plen         = params_end - params_begin;

	const char *res          = params_end + 1; // skip ')'. res is already \0-terminated.

	if (strcmp(member_name, "<init>") != 0) {
		obstack_1grow(&mobst, 'J');
		mangle_types(res, &mobst, &ct);
	}

	if (plen == 0) {
		obstack_1grow(&mobst, 'v');
	} else {
		const char *params = duplicate_string_n(params_begin, plen);
		mangle_types(params, &mobst, &ct);
		free((char*)params);
	}

name_finished: ;
	size_t  result_len    = obstack_object_size(&mobst);
	char   *result_string = obstack_finish(&mobst);
	ident  *result        = new_id_from_chars(result_string, result_len);
	obstack_free(&mobst, result_string);

	mangle_ct_flush(&ct);

	return result;
}

ident *mangle_vtable_name(const char *classname)
{
	struct obstack mobst;
	obstack_init(&mobst);

	compression_table_t ct;
	mangle_ct_init(&ct);

	obstack_grow(&mobst, mangle_prefix, strlen(mangle_prefix));
	obstack_grow(&mobst, "_ZTV", 4);

	int emitted_N = mangle_qualified_class_name(classname, false, &mobst, &ct);
	assert(emitted_N);

	obstack_1grow(&mobst, 'E');

	size_t  result_len    = obstack_object_size(&mobst);
	char   *result_string = obstack_finish(&mobst);
	ident  *result        = new_id_from_chars(result_string, result_len);
	obstack_free(&mobst, result_string);

	mangle_ct_flush(&ct);
	return result;
}

void mangle_init(void)
{
	cpset_init(&st, string_hash, string_cmp);

	mangle_add_name_substitution("<init>", "C1");
	mangle_add_name_substitution("<clinit>", "18__U3c_clinit__U3e_");
	mangle_add_name_substitution("and", "4and$");
	mangle_add_name_substitution("or", "3or$");
	mangle_add_name_substitution("not", "4not$");
	mangle_add_name_substitution("xor", "4xor$");
	mangle_add_name_substitution("delete", "7delete$");
}

void mangle_deinit(void)
{
	cpset_iterator_t iter;
	cpset_iterator_init(&iter, &st);

	st_entry *cur_ste;
	while ( (cur_ste = (st_entry*)cpset_iterator_next(&iter)) != NULL) {
		free_ste(cur_ste);
	}

	cpset_destroy(&st);
}
