/* Platform specific typedefs
 *
 * This file defines data types used for accessing external data,
 * i.e., stuff that is a specific size.  You may need to edit this
 * to make the field sizes work right for a particular platform.
 * (Likely cases are noted below.)
 */

/* We'll try to figure out the sizes automatically.  If this doesn't
 * work right, define SHORTSIZE, LONGSIZE, and INTSIZE to equal the
 * size (in bytes) of "short int", "long int" and "int" respectively.
 */

#include <limits.h>

#ifndef SHORTSIZE
#if (SHRT_MAX == 0x7fff)
#define SHORTSIZE 2
#else
#error "Please define SHORTSIZE"
#endif
#endif

#ifndef INTSIZE
#if (INT_MAX == 0x7fff)
#define INTSIZE 2
#else
#if (INT_MAX == 0x7fffffff)
#define INTSIZE 4
#else
#error "Please define INTSIZE"
#endif
#endif
#endif

#ifndef LONGSIZE
#if (LONG_MAX == 0x7fffffff)
#define LONGSIZE 4
#else
#if (LONG_MAX == 0x7fffffffffffffff)
#define LONGSIZE 8
#else
#error "Please define LONGSIZE"
#endif
#endif
#endif

#if (SHORTSIZE == 2)
typedef unsigned short int	word16;
typedef short int		int16;
#else
#error "can't typedef 16-bit integers..."
#endif

#if (LONGSIZE == 4)
typedef unsigned long int	lword32;
typedef long int		long32;
#else
#if (INTSIZE == 4)
typedef unsigned int		lword32;
typedef int			long32;
#else
#error "can't typedef 32-bit integers..."
#endif
#endif
