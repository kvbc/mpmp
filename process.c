/*
 *
 * process.c
 * 
 * Macro-processor's backend
 *
 */

#include "mp.h"

#include <ctype.h>
#include <string.h>

static int process (struct mp_ProcessEnv* pe, MP_BOOL writeNL, MP_BOOL ismain);
static char PE_advance (struct mp_ProcessEnv* pe);
static int PE_next_delim (struct mp_ProcessEnv* pe, enum mp_DelimWhat what);

/*
 *
 * PE
 * 
 */

// set state defaults
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

	pe->macrostop = 0;
	pe->stringstop = 0;
	pe->tofreetop = 0;
	pe->fn = fn;

	pe->ctx.src = src;
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

// free pe->tofree
void mp_PE_free (struct mp_ProcessEnv* pe)
{
	while (pe->tofreetop > 0)
		free(pe->tofree[--pe->tofreetop]);
}

/*
 *
 * PE :: Source helpers
 * 
 */

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

// is word beggining
static MP_BOOL is_wordbegc (char c) {
	return (
		(c == '_') ||
		isalpha(c)
	) ? MP_TRUE : MP_FALSE;
}
// is word char (not beggining)
static MP_BOOL is_wordc (char c) {
	return (
		(c == '_') ||
		isalnum(c)
	) ? MP_TRUE : MP_FALSE;
}
// is a horizontal whitespace
static MP_BOOL is_Hws (char c) {
	return (
		(c == ' ')  ||
        (c == '\t') ||
		(c == '\v') ||
		(c == '\f')
	) ? MP_TRUE : MP_FALSE;
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
 * PE :: Source analysis
 *
 */

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

/*
 *
 * PE :: Macros
 * 
 */

// returns macro/NULL
static struct mp_Macro* PE_next_macro (struct mp_ProcessEnv* pe)
{
	if (pe->macrostop >= MP_MAX_MACROS) {
		MP_PRINT_PROCESS_ERROR(pe, "Maximum number of macros (%u) exceeded", MP_MAX_MACROS);
		return NULL;
	}

	struct mp_Macro* macro = &pe->macros[pe->macrostop++];
	macro->exp = MP_FALSE;
	macro->isfunc = MP_FALSE;
	macro->def = NULL;
	macro->deflen = 0;
	macro->name = NULL;
	macro->namelen = 0;
	macro->params = NULL;
	macro->paramc = 0;

	return macro;
}

static struct mp_Macro* PE_find_macro (struct mp_ProcessEnv* pe, const char* name, size_t len)
{
	for (size_t i = 0; i < pe->macrostop; i++)
	{
		struct mp_Macro* macro = &pe->macros[i];
		if (mp_cstr_eq(name, len, macro->name, macro->namelen) == MP_TRUE)
			return macro;
	}
	return NULL;
}

// expand macro
// store result in pe->state.word, pe->state.wlen
// returns MP_OK/MP_BAD
static int PE_expand_macro (struct mp_ProcessEnv* pe, struct mp_Macro* macro)
{
	if (macro == NULL)
		return MP_OK;

	pe->state.word = NULL;
	pe->state.wlen = 0;

	if (PE_char(pe) == '(') {
		PE_advance(pe);

		size_t argc = 0;
		size_t oldmacrostop = pe->macrostop;

		// turn arguments into macros, calculate argc
		for (size_t i = 0;; i++) {
			int ret = PE_next_delim(pe, MP_DELIM_ARGS);
			if (ret == MP_BAD)
				return MP_BAD;

			struct mp_String* param = &macro->params[i];
			struct mp_Macro* argm = PE_next_macro(pe);
			if (argm == NULL)
				return MP_BAD;
			argm->name = param->buff;
			argm->namelen = param->len;
			argm->def = pe->state.word;
			argm->deflen = pe->state.wlen;
			argc++;

			if (ret == MP_END)
				break;
		}

		char* outbuff = malloc(sizeof(char) * MP_MAX_DEF_LEN);

		struct mp_ProcessState oldps = pe->state;
		struct mp_ProcessContext oldpc = pe->ctx;
		pe->ctx.src = macro->def;
		pe->ctx.readlen = macro->deflen;
		pe->ctx.outBuff = outbuff;
		pe->ctx.endch = MP_ENDCH_NONE;
		PE_reset_state(pe);
		if (process(pe, MP_FALSE, MP_FALSE) == MP_BAD) {
			free(outbuff);
			return MP_BAD;
		}
		oldps.word = outbuff;
		oldps.wlen = pe->state.outofs;
		pe->ctx = oldpc;
		pe->state = oldps;

		// TODO this sucks, wasting memory
		for (size_t i = 0; i < argc; i++)
			pe->macros[oldmacrostop + i].namelen = 0; // ignore macro arguments

		return MP_OK;
	}

	char* outbuff = malloc(sizeof(char) * MP_MAX_DEF_LEN);

	struct mp_ProcessState oldps = pe->state;
	struct mp_ProcessContext oldpc = pe->ctx;
	pe->ctx.src = macro->def;
	pe->ctx.readlen = macro->deflen;
	pe->ctx.outBuff = outbuff;
	pe->ctx.endch = MP_ENDCH_NONE;
	PE_reset_state(pe);
	if (process(pe, MP_FALSE, MP_FALSE) == MP_BAD) {
		free(outbuff);
		return MP_BAD;
	}
	oldps.word = outbuff;
	oldps.wlen = pe->state.outofs;
	pe->ctx = oldpc;
	pe->state = oldps;

	return MP_OK;
}

// read all characters from start to )/,/EOF
// result stored in pe->state.word and pe->state.wlen
static void PE_read_delim (struct mp_ProcessEnv* pe, const char* start)
{
	char c = PE_char(pe);
	while (
		(c != ')') &&
		(c != ',') &&
		(pe->state.eof == MP_FALSE)
	) c = PE_advance(pe);
	pe->state.word = start;
	pe->state.wlen = PE_charPtr(pe) - start - pe->state.nllen;
}

// opening '(' must be read
// read arg (what == MP_DELIM_ARGS) or param (what == MP_DELIM_PARAMS)
// result stored in pe->state.word and pe->state.wlen
// returns MP_OK/MP_BAD or MP_END if ended
static int PE_next_delim (struct mp_ProcessEnv* pe, enum mp_DelimWhat what)
{
	PE_skip_Hws(pe);

	if (PE_char(pe) == ')') {
		PE_advance(pe);
		return MP_END;
	}

	if (what == MP_DELIM_PARAMS)
	{
		if (PE_word(pe) == MP_BAD) { // result
			MP_PRINT_PROCESS_ERROR(pe, "Malformed macro parameter \"%.*s\"", pe->state.wlen, pe->state.word);
			return MP_BAD;
		}
	}
	else { // MP_DELIM_ARGS
		const char* start = PE_charPtr(pe);
		if (PE_word(pe) == MP_OK) { // macro/result
			struct mp_Macro* macro = PE_find_macro(pe, pe->state.word, pe->state.wlen);
			if (macro != NULL) {
				if (PE_expand_macro(pe, macro) == MP_OK) { // result
					if (pe->tofreetop >= MP_MAX_MACRO_EXPS) {
						MP_PRINT_PROCESS_ERROR(pe, "Maximum number of macro expansions (%u) exceeded", MP_MAX_MACRO_EXPS);
						return MP_BAD;
					}
					pe->tofree[pe->tofreetop++] = pe->state.word; // allocated expanded definition, free later
				}
				else PE_read_delim(pe, start); // result
			}
			else PE_read_delim(pe, start); // result
		}
		else PE_read_delim(pe, start); // result
	}

	PE_skip_Hws(pe);
	char c = PE_char(pe);
	PE_advance(pe);

	if (c == ')')
		return MP_END;
	if (c != ',') {
		MP_PRINT_PROCESS_ERROR(pe, "Missing separator ',' while processing macro");
		return MP_BAD;
	}

	return MP_OK;
}

/*
 *
 * Process
 * 
 */

int mp_process (struct mp_ProcessEnv* pe)
{
	return process(pe, MP_TRUE, MP_TRUE);
}

static int process (struct mp_ProcessEnv* pe, MP_BOOL writeNL, MP_BOOL ismain)
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
		else if (is_wordbegc(c)) {
			PE_writeall(pe);
			PE_word(pe);
			if (pe->state.isinstr == MP_TRUE) {
				pe->state.isinstr = MP_FALSE;
				if (mp_cstr_eq(pe->state.word, pe->state.wlen, "define", 6) == MP_TRUE) {
					PE_skip_Hws(pe);
					if (PE_word(pe) == MP_BAD) {
						MP_PRINT_PROCESS_ERROR(pe, "Malformed macro identifier \"%.*s\"", pe->state.wlen, pe->state.word);
						return MP_BAD;
					}
					PE_skip_Hws(pe);
					
					// init macro
					struct mp_Macro* macro = PE_next_macro(pe);
					if (macro == NULL)
						return MP_BAD;
					macro->name = pe->state.word;
					macro->namelen = pe->state.wlen;

					// init function-like macro
					if (PE_char(pe) == '(') {
						PE_advance(pe);
						macro->isfunc = MP_TRUE;
						macro->params = &pe->strings[pe->stringstop];
						
						for (int ret;;) {
							ret = PE_next_delim(pe, MP_DELIM_PARAMS);
							if (ret == MP_BAD)
								return MP_BAD;

							if (pe->stringstop >= MP_MAX_MACROS) {
								MP_PRINT_PROCESS_ERROR(pe, "Maximum number of strings (%u) exceeded", MP_MAX_MACROS);
								return MP_BAD;
							}
							struct mp_String* marg = &pe->strings[pe->stringstop++];
							marg->buff = pe->state.word;
							marg->len = pe->state.wlen;
							
							macro->paramc++;
							if (ret == MP_END)
								break;
						}
					}

					// set macro's definition
					PE_skip_Hws(pe);
					macro->def = PE_charPtr(pe);
					PE_skip_line(pe);
					macro->deflen = PE_charPtr(pe) - macro->def - pe->state.nllen;
				}
				else {
					MP_PRINT_PROCESS_ERROR(pe, "Undefined instruction \"%.*s\"", pe->state.wlen, pe->state.word);
					return MP_BAD;
				}
			}
			else {
				struct mp_Macro* macro = PE_find_macro(pe, pe->state.word, pe->state.wlen);
				if (PE_expand_macro(pe, macro) == MP_BAD)
					return MP_BAD;
				PE_writestr(pe, pe->state.word, pe->state.wlen);
				if (ismain == MP_TRUE)
					mp_PE_free(pe);
				
				if (writeNL == MP_TRUE)
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