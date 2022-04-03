#include "mp.h"

#include <stdio.h>
#include <stdlib.h>

inline static void print_usage (const char* name) {
	printf("Usage: %s <src> <out>", name);
}

int main (int argc, char* argv[])
{
	if (argc < 2) {
		MP_PRINT_ERROR("No source file specified");
		print_usage(argv[0]);
		return 0;
	}

	if (argc < 3) {
		MP_PRINT_ERROR("No output file specified");
		print_usage(argv[0]);
		return 0;
	}

	if (argc > 4) {
		MP_PRINT_ERROR("Invalid number of arguments");
		print_usage(argv[0]);
		return 0;
	}

	long flen;
	char* src = NULL;
	char* srcfn = argv[1];
	char* outfn = argv[2];

	if (mp_file_read(NULL, srcfn, NULL, &src, &flen, 0, 0, MP_TRUE) == MP_OK) {
		char* out = malloc(sizeof(*out) * (flen + 1));
		struct mp_ProcessEnv pe;
		mp_PE_init(&pe, src, srcfn, out, flen, 0, MP_ENDCH_NONE);
		if (mp_process(&pe) == MP_OK)
			mp_file_write(NULL, outfn, pe.ctx.outBuff, pe.state.outofs);
		mp_PE_free(&pe);
		free(out);
	}

	free(src);
	return 0;
}