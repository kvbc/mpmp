/*
 *
 * file.c
 * 
 * Wrappers for file I/O.
 * 
 */

#include "mp.h"

#include <stdio.h>
#include <stdlib.h>

/*
 *
 * Read the file into a buffer.
 * 
 */
int mp_file_read (
	FILE* f,				// if NULL, file opened with 'filename'
	const char* filename,   // if NULL, file opened with 'f'
	char* buff,				// if NULL, *optBuff allocated and used (output buffer)
	char** optBuff,			// if NULL, buff is used
	long* flenPtr,          // if not NULL, set to file length
	long offset,			// offset to read from
    size_t readlen,			// if 0, set to file lenth
    MP_BOOL nullterm		// should buff/optBuff be null terminated
) {
	// no file stream specified, try to open the file using the given filename
	if (f == NULL) {
		if (filename == NULL) {
			MP_PRINT_ERROR("Failed to read file \"%s\": no file stream or filename provided", filename);
			return MP_BAD;
		}
		f = fopen(filename, "rb");
		if (f == NULL) {
			MP_PRINT_ERROR("Failed to open file \"%s\" for reading", filename);
			return MP_BAD;
		}
	}

	int ret = MP_OK;

	if (
		(readlen == 0) ||
		(flenPtr != NULL)
	) { // have to calculate the file length
		// seek to the end for ftell() to be able to return the file length
		if (fseek(f, 0, SEEK_END)) {
			MP_PRINT_ERROR("Failed to seek in file \"%s\"", filename);
			ret = MP_BAD;
			goto close_file;
		}

		long flen = ftell(f);
		if (readlen == 0)
			readlen = flen;
		if (flenPtr != NULL)
			*flenPtr = flen;
	}

	// seek to the given offset for fread()
	if (fseek(f, offset, SEEK_SET)) {
		MP_PRINT_ERROR("Failed to seek in file \"%s\"", filename);
		ret = MP_BAD;
		goto close_file;
	}

	/*
	 *
	 * Establish the output buffer
	 * 
	 */
	if (optBuff != NULL) {
		if (nullterm == MP_FALSE)
			*optBuff = malloc(sizeof(char) * readlen);
		else {
			*optBuff = malloc(sizeof(char) * (readlen + 1));
			(*optBuff)[readlen] = '\0';
		}
		if (*optBuff == NULL) {
			MP_PRINT_ERROR("Out of memory while reading file \"%s\"", filename);
			ret = MP_BAD;
			goto close_file;
		}
		if (buff == NULL)
			buff = *optBuff;
	}
	else if (buff != NULL)
		if (nullterm == MP_TRUE)
			buff[readlen] = '\0';
	if (buff == NULL) {
		MP_PRINT_ERROR("No buffer specified for reading file \"%s\"", filename);
		ret = MP_BAD;
		goto close_file;
	}

	// write file contents into the buffer
	if (fread(buff, sizeof(char), readlen, f) != readlen) {
		MP_PRINT_ERROR("Failed to properly read file \"%s\"", filename);
		ret = MP_BAD;
		goto close_file;
	}

close_file:
	// close the file, if opened by this function
	if (filename != NULL)
	if (fclose(f) == EOF) {
		MP_PRINT_ERROR("Failed to close file \"%s\"", filename);
		return MP_BAD;
	}

	return ret;
}

/*
 *
 * Write the buffer into the file
 *
 */
int mp_file_write (
	FILE* f,				// if NULL, file opened with 'filename'
	const char* filename,	// if NULL, file opened with 'f'
	const char* buff,		// buffer to write
	size_t len				// length of the buffer
) {
	// no file stream specified, try to open a file using the given filename
	if (f == NULL) {
		if (filename == NULL) {
			MP_PRINT_ERROR("Failed to write to file \"%s\": no file stream or filename provided", filename);
			return MP_BAD;
		}
		f = fopen(filename, "wb");
		if (f == NULL) {
			MP_PRINT_ERROR("Failed to open file \"%s\" for writing", filename);
			return MP_BAD;
		}
	}

	int ret = MP_OK;

	// write buffer contents into the file
	if (fwrite(buff, sizeof(char), len, f) != len) {
		MP_PRINT_ERROR("Failed to properly write to file \"%s\"", filename);
		ret = MP_BAD;
	}

	// close the file, if opened by this function
	if (filename != NULL)
	if (fclose(f) == EOF) {
		MP_PRINT_ERROR("Failed to close file \"%s\"", filename);
		return MP_BAD;
	}

	return ret;
}