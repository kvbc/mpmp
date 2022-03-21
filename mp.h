/*
 *
 * mp.h
 * Global declarations and definitions
 * 
 */

#ifndef MP_H
#define MP_H

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#define MP_BOOL  int
#define MP_TRUE  1
#define MP_FALSE 0

#define MP_OK  MP_TRUE
#define MP_BAD MP_FALSE

#define MP_PRINT_ERROR(frmt, ...)             (printf(("Error: "   frmt "\n") __VA_OPT__(,) __VA_ARGS__))
#define MP_PRINT_WARNING(frmt, ...)			  (printf(("Warning: " frmt "\n") __VA_OPT__(,) __VA_ARGS__))
#define MP_PRINT_PROCESS_ERROR(pc, frmt, ...) (MP_PRINT_ERROR(frmt " at offset %u (ln:%u col:%u), while processing file \"%s\"" __VA_OPT__(,) __VA_ARGS__, (pc)->state.srcofs + 1, (pc)->state.ln, (pc)->state.srcofs - (pc)->state.lnsidx + 1, (pc)->ctx.fn))

// file
int mp_file_read  (FILE* f, const char* filename, char* buff, char** optBuff, long* flenPtr, long offset, size_t readlen, MP_BOOL nullterm);
int mp_file_write (FILE* f, const char* filename, const char* buff, size_t len);

struct mp_String {
	const char* buff;
	size_t len;
};

/*
 *
 * process
 *
 */

struct mp_Macro {
	const char* name;
	size_t namelen;
	char* def;
	size_t deflen;
	MP_BOOL isfunc;
	struct mp_String* args;
	size_t argslen;
};

struct mp_ProcessState {
	size_t srcofs;
	size_t outofs;
	MP_BOOL eof;
	size_t ln;
	size_t lnsidx; // line start index
	MP_BOOL isinstr;
	size_t nllen;
	const char* nlstr;
	const char* word;
	size_t wlen;
	const char* writestart;
};

struct mp_ProcessContext {
	const char* src;
	const char* fn;
	char* outBuff;
	size_t readlen;
	int endch;
};

struct mp_ProcessEnv {
	struct mp_ProcessState state;
	struct mp_ProcessContext ctx;
	size_t macrotop;
	struct mp_Macro macros[MP_MAX_MACROS];
	size_t macroargstop;
	struct mp_String macroargs[MP_MAX_MACRO_ARGS];
};

#define MP_ENDCH_NONE SCHAR_MIN - 1
#define MP_ENDCH_NL   SCHAR_MIN - 2
void mp_PE_init (struct mp_ProcessEnv* pe, const char* src, const char* fn, char* outBuff, size_t outBuffLen, size_t readlen, int endch);
void mp_PE_free (struct mp_ProcessEnv* pe);
int mp_process (struct mp_ProcessEnv* pe);

// cstr
MP_BOOL mp_cstr_eq (const char* str1, size_t len1, const char* str2, size_t len2);

#endif // MP_H