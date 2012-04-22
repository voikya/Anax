#ifndef GLOBALS_H
#define GLOBALS_H

#define ANAX_ERR_INVALID_INVOCATION					-1
#define ANAX_ERR_FILE_DOES_NOT_EXIST				-2
#define ANAX_ERR_NO_MEMORY							-3
#define ANAX_ERR_TIFF_SCANLINE						-4
#define ANAX_ERR_PNG_STRUCT_FAILURE					-5
#define ANAX_ERR_INVALID_COLOR_FILE					-6
#define ANAX_ERR_COULD_NOT_RESOLVE_ADDR				-7
#define ANAX_ERR_COULD_NOT_CONNECT					-8

#define ANAX_RELATIVE_COLORS						0
#define ANAX_ABSOLUTE_COLORS						1

#define ANAX_STATE_PENDING							1
#define ANAX_STATE_INPROGRESS						2
#define ANAX_STATE_COMPLETE							3
#define ANAX_STATE_NOJOB							4
#define ANAX_STATE_LOST								5

#define BUFSIZE										1024
#define REMOTE_PORT									"51777"

#endif

