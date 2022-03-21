/*
 *
 * process.c
 * 
 * Macro-processor's backend
 * 
 * ============== NOTES ==============
 * - process       : wasting memory "ignoring" macros (when writing macro definitions to the output buffer), instead of popping them off
 * - PE_find_macro : perhaps hash macro names for increased performance
 *
 */

#include "mp.h"

#include <ctype.h>
#include <string.h>

static int process (struct mp_ProcessEnv* pe, MP_BOOL writeNL);
static void PE_pop_macros (struct mp_ProcessEnv* pe, size_t count);

/*
 *
 * PE
 * 
 */

// set PE's state defaults
static void PE_reset_state (struct mp_ProcessEnv* pe)
{
	pe->state.ln = 1;
	pe->state.lnsidx = 0;
	pe->state.srcofs = 0;
	pe->state.outofs = 0;
	pe->state.nllen = 0;
	pe->state.eof = MP_FALSE;
	pe->state.isinstr = MP_FALSE;
	pe->state.writestart = NULL;
	pe->state.nlstr = NULL;
}

void mp_PE_init (
	struct mp_ProcessEnv* pe,
	const char* src,
	const char* fn,				// if NULL, set to "UNNAMED"
	char* outBuff,
	size_t outBuffLen,
	size_t readlen,				// if 0 or higher than outBuffLen, set to outBuffLen
	int endch
) {
	if (fn == NULL)
		fn = "UNNAMED";

	pe->macrotop = 0;
	pe->macroargstop = 0;

	pe->ctx.src = src;
	pe->ctx.fn = fn;
	pe->ctx.outBuff = outBuff;
	pe->ctx.endch = endch;

	PE_reset_state(pe);

	if (readlen > outBuffLen)
		MP_PRINT_WARNING("'readlen' (%zu) exceeds 'outlen' (%zu) for '%s'", readlen, outBuffLen, fn);
	if (
		(readlen == 0) ||
		(readlen > outBuffLen)
	)
		 pe->ctx.readlen = outBuffLen;
	else pe->ctx.readlen = readlen;
}

void mp_PE_free (struct mp_ProcessEnv* pe)
{
	PE_pop_macros(pe, pe->macroargstop);	
}

// helpers for moving in the source
static inline const char* PE_charPtr (struct mp_ProcessEnv* pe) {
	return &pe->ctx.src[pe->state.srcofs];
}
static inline const char* PE_advCharPtr (struct mp_ProcessEnv* pe) {
	return &pe->ctx.src[++pe->state.srcofs];
}
static inline char PE_char (struct mp_ProcessEnv* pe) {
	return * PE_charPtr(pe);
}
static inline char PE_advChar (struct mp_ProcessEnv* pe) {
	return * PE_advCharPtr(pe);
}

/*
 *
 * PE :: Source
 *
 */

// expand a macro definition
// def & deflen must be known
static int PE_expand (struct mp_ProcessEnv* pe, struct mp_Macro* macro)
{
	// expanded definition
	char* expdef = malloc(sizeof(char) * MP_MAX_DEF_LEN); // freed in PE_pop_macros

	struct mp_ProcessState oldps = pe->state;
	struct mp_ProcessContext oldpc = pe->ctx;
	pe->ctx.outBuff = expdef;
	pe->ctx.src = macro->def;
	pe->ctx.readlen = macro->deflen;
	pe->ctx.endch = MP_ENDCH_NONE;
	PE_reset_state(pe);
	if (process(pe, MP_FALSE) == MP_BAD)
		return MP_BAD;
	macro->def = expdef;
	macro->deflen = pe->state.outofs;
	pe->ctx = oldpc;
	pe->state = oldps;

	return MP_OK;
}

// advance to the next character
// accounts for EOF and new lines
// returns the next character, or '\0' in case of EOF
static char PE_advance (struct mp_ProcessEnv* pe)
{
	if (pe->state.srcofs >= pe->ctx.readlen) {
		pe->state.eof = MP_TRUE;
		return '\0';
	}

	pe->state.nllen = 0;
	char c = PE_advChar(pe);

	if (c == pe->ctx.endch)
		pe->state.eof = MP_TRUE;

	if (c == '\n') {
		pe->state.nlstr = "\n";
		pe->state.ln++;
		pe->state.nllen = 1;
		pe->state.lnsidx = pe->state.srcofs;
		if (pe->ctx.endch == MP_ENDCH_NL)
			pe->state.eof = MP_TRUE;
		return c;
	}
	if (c == '\r') {
		pe->state.ln++;
		if (PE_advChar(pe) == '\n') {
			pe->state.nlstr = "\r\n";
			pe->state.nllen = 2;
			PE_advCharPtr(pe);
		}
		else {
			pe->state.nlstr = "\r";
			pe->state.nllen = 1;
		}
		pe->state.lnsidx = pe->state.srcofs;
		if (pe->ctx.endch == MP_ENDCH_NL)
			pe->state.eof = MP_TRUE;
		return c;
	}

	return c;
}

static MP_BOOL is_wordbegc (char c) {
	return (
		(c == '_') ||
		isalpha(c)
	);
}

static MP_BOOL is_wordc (char c) {
	return (
		(c == '_') ||
		isalnum(c)
	);
}

// read the word into state.word, state.wlen
// returns MP_OK/MP_BAD
static int PE_word (struct mp_ProcessEnv* pe)
{
	int ret = MP_OK;
	pe->state.word = PE_charPtr(pe);
	char c = PE_char(pe);

	if (!is_wordbegc(c)) {
		c = PE_advance(pe);
		ret = MP_BAD;
	}

	while (is_wordc(c))
		c = PE_advance(pe);
	pe->state.wlen = PE_charPtr(pe) - pe->state.word - pe->state.nllen;

	return ret;
}

// is character a horizontal whitespace
static MP_BOOL is_Hws (char c) {
	return (
		(c == ' ')  ||
        (c == '\t') ||
		(c == '\v') ||
		(c == '\f')
	) ? (MP_TRUE) : (MP_FALSE);
}

// skip horizontal whitespace
static void PE_skip_Hws (struct mp_ProcessEnv* pe)
{
	while (
		(!pe->state.eof) &&
		(is_Hws(PE_char(pe)))
	) PE_advance(pe);
}

static void PE_skip_line (struct mp_ProcessEnv* pe)
{
	size_t ln = pe->state.ln;
	while (
		(!pe->state.eof) &&
		(pe->state.ln == ln)
	) PE_advance(pe);
}

/*
 *
 * PE :: Macros
 * 
 */

static void PE_pop_macros (struct mp_ProcessEnv* pe, size_t count)
{
	while (count--)
	{
		free(pe->macros[pe->macrotop--].def);
	}
}

static struct mp_Macro* PE_next_macro (struct mp_ProcessEnv* pe)
{
	if (pe->macrotop >= MP_MAX_MACROS) {
		MP_PRINT_PROCESS_ERROR(pe, "Maximum number of macros (%u) exceeded", MP_MAX_MACROS);
		return NULL;
	}
	struct mp_Macro* macro = &pe->macros[pe->macrotop++];
	macro->isfunc = MP_FALSE;
	macro->def = NULL;
	macro->deflen = 0;
	macro->name = NULL;
	macro->namelen = 0;
	macro->args = NULL;
	macro->argslen = 0;
	return macro;
}

// if a macro with the given name exists, return it, otherwise return NULL
static struct mp_Macro* PE_find_macro (struct mp_ProcessEnv* pe, const char* name, size_t len)
{
	for (size_t i = 0; i < pe->macrotop; i++)
	{
		struct mp_Macro* macro = &pe->macros[i];
		if (mp_cstr_eq(name, len, macro->name, macro->namelen) == MP_TRUE)
			return macro;
	}
	return NULL;
}

// if params == MP_TRUE, push paramters onto pe->macroargs, otherwise push arguments
// if count != NULL, set it to the number of pushed stuff
// returns MP_OK/MP_BAD
static int PE_push_macro_delim (struct mp_ProcessEnv* pe, MP_BOOL params, size_t* count)
{
	size_t oldtop = pe->macroargstop;

	for (;;) {
		PE_skip_Hws(pe);

		if (PE_char(pe) == ')') {
			PE_advance(pe);
			break;
		}

		if (params == MP_TRUE) {
			if (PE_word(pe) == MP_BAD) {
				MP_PRINT_PROCESS_ERROR(pe, "Malformed macro parameter \"%.*s\"", pe->state.wlen, pe->state.word);
				return MP_BAD;
			}
		} else {
			char c = PE_char(pe);
			pe->state.word = PE_charPtr(pe);
			pe->state.wlen = 0;
			int br = 1;
			for (;;) {
				c = PE_advance(pe);
				if (pe->state.eof == MP_TRUE) {
					MP_PRINT_PROCESS_ERROR(pe, "Unexpected EOF while processing macro arguments");
					return MP_BAD;
				}
				if (c == '(')
					br++;
				else if (c == ')')
					br--;
				if (br < 0) {
					MP_PRINT_PROCESS_ERROR(pe, "Unmatched parentheses");
					return MP_BAD;
				}
				if (br == 0) // ending )
					break;
				if (br == 1)
				if (c == ',')
					break;
			}
			pe->state.wlen = PE_charPtr(pe) - pe->state.word;
		}
		
		if (pe->macroargstop >= MP_MAX_MACRO_ARGS) {
			MP_PRINT_PROCESS_ERROR(pe, "Maximum number of macro arguments (%u) exceeded", MP_MAX_MACRO_ARGS);
			return MP_BAD;
		}

		struct mp_String* arg = &pe->macroargs[pe->macroargstop++];
		arg->buff = pe->state.word;
		arg->len = pe->state.wlen;

		PE_skip_Hws(pe);
		char c = PE_char(pe);
		PE_advance(pe);

		if (c == ')')
			break;
		if (c != ',') {
			MP_PRINT_PROCESS_ERROR(pe, "Missing separator ',' while processing macro");
			return MP_BAD;
		}
	}

	if (count != NULL)
		*count = pe->macroargstop - oldtop;

	return MP_OK;
}

/*
 *
 * PE :: Output
 * 
 */

static void PE_writestr (struct mp_ProcessEnv* pe, const char* str, size_t len)
{
	strncpy(&pe->ctx.outBuff[pe->state.outofs], str, len);
	pe->state.outofs += len;
}

// write from pe->writestart to now
static void PE_writeall (struct mp_ProcessEnv* pe)
{
	if (pe->state.writestart == NULL)
		return;
	if (PE_charPtr(pe) == pe->state.writestart) {
		pe->state.writestart = NULL;
		return;
	}
	PE_writestr(pe, pe->state.writestart, PE_charPtr(pe) - pe->state.writestart - pe->state.nllen);
	pe->state.writestart = NULL;
}

// write latest new-line
static inline void PE_writenl (struct mp_ProcessEnv* pe)
{
	PE_writestr(pe, pe->state.nlstr, pe->state.nllen);
}

/*
 *
 * Process
 * 
 */

int mp_process (struct mp_ProcessEnv* pe)
{
	return process(pe, MP_TRUE);
}

static int process (struct mp_ProcessEnv* pe, MP_BOOL writeNL)
{
	for (; !pe->state.eof ;)
	{
		char c = PE_char(pe);
		/*
		 *
		 * instruction start
		 * 
		 */
		if (c == MP_INSTRUCTION_PREFIX) {
			PE_writeall(pe);
			pe->state.isinstr = MP_TRUE;
			PE_advance(pe);
		}
		/*
		 *
		 * word
		 * 
		 */
		else if (isalpha(c)) {
			PE_writeall(pe);
			PE_word(pe);
			if (pe->state.isinstr == MP_TRUE) {
				pe->state.isinstr = MP_FALSE;
				if (mp_cstr_eq(pe->state.word, pe->state.wlen, "define", 6) == MP_TRUE) {
					PE_skip_Hws(pe);
					if(PE_word(pe) == MP_BAD) {
						MP_PRINT_PROCESS_ERROR(pe, "Malformed macro identifier \"%.*s\"", pe->state.wlen, pe->state.word);
						return MP_BAD;
					}
					PE_skip_Hws(pe);
					
					struct mp_Macro* macro = PE_next_macro(pe);
					if (macro == NULL)
						return MP_BAD;
					macro->name = pe->state.word;
					macro->namelen = pe->state.wlen;

					if (PE_char(pe) == '(') {
						PE_advance(pe);
						macro->isfunc = MP_TRUE;
						macro->args = &pe->macroargs[pe->macroargstop];
						if (PE_push_macro_delim(pe, MP_TRUE, &macro->argslen) == MP_BAD)
							return MP_BAD;
					}

					PE_skip_Hws(pe);
					macro->def = PE_charPtr(pe);
					PE_skip_line(pe);
					macro->deflen = PE_charPtr(pe) - macro->def - pe->state.nllen;
					
					if (PE_expand(pe, macro) == MP_BAD)
						return MP_BAD;
				}
				else {
					MP_PRINT_PROCESS_ERROR(pe, "Undefined instruction \"%.*s\"", pe->state.wlen, pe->state.word);
					return MP_BAD;
				}
			}
			else {
				struct mp_Macro* macro = PE_find_macro(pe, pe->state.word, pe->state.wlen);

				if (macro != NULL)
				if (PE_char(pe) == '(') {
					PE_advance(pe);

					size_t argc;
					size_t oldmacrotop = pe->macrotop;
					size_t oldtop = pe->macroargstop;
					if (PE_push_macro_delim(pe, MP_FALSE, &argc) == MP_BAD)
						return MP_BAD;

					for (size_t i = 0; i < argc; i++) {
						struct mp_String* param = &macro->args[i];
						struct mp_String* arg = &pe->macroargs[oldtop + i];
						struct mp_Macro* argm = PE_next_macro(pe);
						if (argm == NULL)
							return MP_BAD;

						argm->name = param->buff;
						argm->namelen = param->len;
						argm->def = arg->buff;
						argm->deflen = arg->len;

						if (PE_expand(pe, argm) == MP_BAD)
							return MP_BAD;
					}

					struct mp_ProcessState oldps = pe->state;
					struct mp_ProcessContext oldpc = pe->ctx;
					pe->ctx.src = macro->def;
					pe->ctx.readlen = macro->deflen;
					pe->ctx.endch = MP_ENDCH_NONE;
					PE_reset_state(pe);
					pe->state.outofs = oldps.outofs;
					if (process(pe, MP_FALSE) == MP_BAD)
						return MP_BAD;
					oldps.outofs = pe->state.outofs;
					pe->ctx = oldpc;
					pe->state = oldps;

					// TODO this sucks, wasting memory
					for (size_t i = 0; i < argc; i++)
						pe->macros[oldmacrotop + i].namelen = 0;

					continue;
				}

				if (macro == NULL)
					PE_writestr(pe, pe->state.word, pe->state.wlen);
				else
					PE_writestr(pe, macro->def, macro->deflen);
				if (writeNL)
					PE_writenl(pe);
			}
		}
		/*
		 *
		 * other
		 * 
		 */
		else {
			if (pe->state.writestart == NULL)
				pe->state.writestart = PE_charPtr(pe);
			PE_advance(pe);
		}
	}

	PE_writeall(pe);

	return MP_OK;
}