#ifndef XTYPES_H
#define XTYPES_H

#include <stdint.h>

typedef int64_t xint64;
typedef uint64_t xuint64;
typedef int32_t xint32;
typedef uint32_t xuint32;
typedef int16_t xint16;
typedef uint16_t xuint16;
typedef int8_t xint8;
typedef uint8_t xuint8;
typedef double xreal64;

typedef xuint8 xbool;
#define XFALSE (0)
#define XTRUE (1)

typedef xuint64 XC_HITIME;
typedef xuint32 XC_VOLUME;

#endif
