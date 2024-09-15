#ifndef ADAPT_H__
#define ADAPT_H__

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>

typedef UINTN size_t;
typedef INTN ptrdiff_t;
typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
typedef INT8 int8_t;
typedef INT16 int16_t;
typedef INT32 int32_t;
typedef INT64 int64_t;
typedef BOOLEAN bool;

#define assert(x) ASSERT (x)
#define memcmp(a, b, l) CompareMem ((a), (b), (l))
#define memcpy(a, b, l) CopyMem ((a), (b), (l))
#define memmove(a, b, l) CopyMem ((a), (b), (l))
#define memset(b, v, l) SetMem ((b), (l), (v))
#define strcpy(d, s) AsciiStrCpyS ((d), AsciiStrSize (s), (s))
#define strlen(s) AsciiStrLen (s)
#define strcasecmp(s, t) AsciiStriCmp ((s), (t))
#define malloc(s) AllocatePool (s)
#define free(p) FreePool (p)

#define false FALSE
#define true TRUE

#define ERROR(...) DEBUG ((DEBUG_ERROR, __VA_ARGS__))
#define INFO(...) DEBUG ((DEBUG_INFO, __VA_ARGS__))
#define WARN(...) DEBUG ((DEBUG_WARN, __VA_ARGS__))

#endif /* ADAPT_H__ */
