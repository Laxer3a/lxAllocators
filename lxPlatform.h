#ifndef LX_PLATFORM_H
#define LX_PLATFORM_H

#if defined(_WIN32) || defined(_WIN64) || defined(OS_WINDOWS)
	#if defined(_MSC_VER)		// Visual Studio
		#include <intrin.h>
	#endif
#endif

namespace lx {

	#if defined(i386) || defined(_M_IX86) || defined(_M_X64)
		#if defined(_MSC_VER)		// Visual Studio
			#if defined(_M_X64)
			// 64 bit ptr
			#define ATOMICINCREMENT(a,b)	_InterlockedExchangeAdd64((volatile __int64*)a,b);
			#else
			// 32 bit ptr
			#define ATOMICINCREMENT(a,b)	_InterlockedExchangeAdd((volatile long*)a,b);
			#endif
		#elif defined(__GNUC__)		// Clang, LLVM, GNU C++, Intel ICC, ICPC
			// GCC
			// __sync_fetch_and_add (type *ptr, type value, ...)
			// __sync_add_and_fetch (type *ptr, type value, ...)
		#endif
	#endif
}

#endif
