/*
 *
 * cstr.c
 * 
 * C-String utils
 * 
 */

#include "mp.h"

/*
 *
 * Is string 'str1' (of length 'len1') equal to string 'str2' (of length 'len2')
 * 
 */
MP_BOOL mp_cstr_eq (const char* str1, size_t len1, const char* str2, size_t len2)
{
	if (len1 != len2)
		return MP_FALSE;
	for (size_t i = 0; i < len1; i++)
		if (str1[i] != str2[i])
			return MP_FALSE;
	return MP_TRUE;
}