#ifndef LX_PLATFORM_LOCK_H
#define LX_PLATFORM_LOCK_H

#if defined(_WIN32) || defined(_WIN64) || defined(OS_WINDOWS)
#include <Windows.h>
#endif

namespace lx {
	#if defined(_WIN32) || defined(_WIN64) || defined(OS_WINDOWS)
		typedef CRITICAL_SECTION		LockType;

		#define CREATELOCK(a)			InitializeCriticalSection(a);
		#define DESTROYLOCK(a)			DeleteCriticalSection(a);
		#define LOCK(a)					EnterCriticalSection(a);
		#define UNLOCK(a)				LeaveCriticalSection(a);
	#endif
}

#endif
