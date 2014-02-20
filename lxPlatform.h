#ifndef LX_PLATFORM_H
#define LX_PLATFORM_H

#if defined(_WIN32) || defined(_WIN64) || defined(OS_WINDOWS)
	#if defined(_MSC_VER)		// Visual Studio
		#include <intrin.h>
		#define USE_WINDOWS_API
	#endif
#endif

namespace lx {

	//
	// Atomic Operations.
	//
	#if defined(USE_WINDOWS_API)
		#if defined(_WIN64)
			// 64 bit ptr
			#define ATOMICINCREMENT32(a,b)	_InterlockedExchangeAdd((volatile long*)a,b);
			#define ATOMICINCREMENTPTR(a,b)	_InterlockedExchangeAdd64((volatile __int64*)a,b);
		#else
			// 32 bit ptr
			#define ATOMICINCREMENT32(a,b)	_InterlockedExchangeAdd((volatile long*)a,b);
			#define ATOMICINCREMENTPTR(a,b)	_InterlockedExchangeAdd((volatile long*)a,b);
		#endif
	#elif defined(__GNUC__)		// Clang, LLVM, GNU C++, Intel ICC, ICPC
		//
		// TODO Increment Atomic 64 and 32 bit with GNU C
		//
	#endif
}

#endif
