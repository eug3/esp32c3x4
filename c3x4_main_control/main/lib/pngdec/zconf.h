/* zconf.h -- configuration of the zlib compression library
 * ESP-IDF Adapted Version
 */

#ifndef ZCONF_H
#define ZCONF_H

// ESP-IDF adaptation - avoid system headers
#define Z_PREFIX_SET
#define STDC 1
#define Z_SOLO 1
#define ZLIB_INTERNAL
#define HAVE_HIDDEN 0

#include <stddef.h>
#include <stdint.h>

// Type definitions for ESP32
typedef unsigned char Byte;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef char charf;
typedef int intf;
typedef uInt uIntf;
typedef uLong uLongf;
typedef void const *voidpc;
typedef void *voidpf;
typedef void *voidp;

typedef unsigned char z_byte;
typedef size_t z_size_t;

// Additional zlib types
typedef Byte Bytef;
typedef char charf;
typedef int intf;
typedef uInt uIntf;
typedef uLong uLongf;
typedef uInt z_crc_t;
typedef unsigned int uint;  // Add missing uint type

// z_const macro
#ifndef z_const
#  define z_const const
#endif

// Off_t for large files
typedef long z_off_t;
typedef long long z_off64_t;

// Function prototypes
#ifndef Z_FAR
#  define Z_FAR
#endif

#ifndef FAR
#  define FAR
#endif

#ifndef Z_EXTERN
#  define Z_EXTERN extern
#endif

// Export macros
#define ZEXPORT
#define ZEXPORTVA
#define ZEXTERN extern

// OF macro for function prototypes
#ifndef OF
#  ifdef STDC
#    define OF(args)  args
#  else
#    define OF(args)  ()
#  endif
#endif

// Z_ARG for variable arguments
#ifndef Z_ARG
#  if defined(STDC) || defined(Z_HAVE_STDARG_H)
#    define Z_ARG(args) args
#  else
#    define Z_ARG(args) ()
#  endif
#endif

// Memory allocation
#ifndef Z_SOLO
#  define ZALLOC(state, items, size) (*((state)->zalloc))((state)->opaque, (items), (size))
#  define ZFREE(state, addr) (*((state)->zfree))((state)->opaque, (voidpf)(addr))
#  define TRY_FREE(s, p) {if (p) ZFREE(s, p);}
#endif

// Error codes
#define Z_OK             0
#define Z_STREAM_END     1
#define Z_NEED_DICT      2
#define Z_ERRNO         (-1)
#define Z_STREAM_ERROR  (-2)
#define Z_DATA_ERROR    (-3)
#define Z_MEM_ERROR     (-4)
#define Z_BUF_ERROR     (-5)
#define Z_VERSION_ERROR (-6)

// Compression levels
#define Z_NO_COMPRESSION         0
#define Z_BEST_SPEED             1
#define Z_BEST_COMPRESSION       9
#define Z_DEFAULT_COMPRESSION  (-1)

// Compression strategies
#define Z_FILTERED            1
#define Z_HUFFMAN_ONLY        2
#define Z_RLE                 3
#define Z_FIXED               4
#define Z_DEFAULT_STRATEGY    0

// Flush values
#define Z_NO_FLUSH      0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH    2
#define Z_FULL_FLUSH    3
#define Z_FINISH        4
#define Z_BLOCK         5
#define Z_TREES         6

// Other constants
#define Z_BINARY   0
#define Z_TEXT     1
#define Z_ASCII    Z_TEXT
#define Z_UNKNOWN  2

#define Z_DEFLATED   8

#define Z_NULL  0

// Window bits
#ifndef MAX_WBITS
#  define MAX_WBITS   15
#endif
#ifndef DEF_WBITS
#  define DEF_WBITS  MAX_WBITS
#endif

// Memory levels
#if MAX_MEM_LEVEL >= 8
#  define DEF_MEM_LEVEL 8
#else
#  define DEF_MEM_LEVEL  MAX_MEM_LEVEL
#endif

// Function pointer types
typedef voidpf (*alloc_func) OF((voidpf opaque, uInt items, uInt size));
typedef void   (*free_func)  OF((voidpf opaque, voidpf address));

// Forward declarations - use opaque pointer for incomplete type
struct internal_state;
struct z_stream_s;

typedef struct z_stream_s z_stream;
typedef z_stream *z_streamp;

// Version info
#define ZLIB_VERSION "1.2.11"
#define ZLIB_VERNUM 0x12b0
#define ZLIB_VER_MAJOR 1
#define ZLIB_VER_MINOR 2
#define ZLIB_VER_REVISION 11
#define ZLIB_VER_SUBREVISION 0

// Compile-time options
#ifndef Z_SOLO
#  include <string.h>
#  include <stdlib.h>
#endif

// memcpy for PROGMEM compatibility
#define memcpy_P memcpy

// PROGMEM for ESP-IDF
#define PROGMEM

// Function prototype macros (needed for zlib)
#define inflateInit(strm) inflateInit_((strm), ZLIB_VERSION, (int)sizeof(z_stream))
#define inflateInit2(strm, windowBits) inflateInit2_((strm), (windowBits), ZLIB_VERSION, (int)sizeof(z_stream))

#endif /* ZCONF_H */
