#include "lxAllocators.h"
#include "lxPlatform.h"

#include <memory>


using namespace lx;



// =================================================
//  Stack Allocator Implementation
// =================================================

void* StackAllocator::allocateStack	(u32 size, u32 alignment) {
	// Contract<Size of ZERO allowed>
	lxAssert(alignment != 0, "Alignment MUST NOT BE ZERO");

	// 1. Find non align part
	// 2. Compute the distance to the next aligned part.
	// 3. If already aligned, no offset

	u32 alignmentMiss = ((u32)m_currPtr) & (alignment - 1);
	// Branchless alignment adjustement.
	// Shift 0 or 32 bit -> Same as multiply by 1 or 0 -> Same as if (cond) { a } else { 0 }
	alignmentMiss = (alignment - alignmentMiss) >> ((alignmentMiss == 0)<<5); 

	// 1. Add alignement offset
	size += alignmentMiss;

	if (m_currPtr + size > m_endPtr) {
		return NULL;
	}
	
	u8* ptr = &m_currPtr[alignmentMiss];
	m_currPtr += size;
	return ptr;
}

void* StackAllocator::allocateStackMT(u32 size, u32 alignment) {
	// Contract<Size of ZERO allowed>
	// Contract<Alignment of ZERO allowed>

	// Different technique here because current pointer 
	// can not be trusted outside of atomic.
	// Force to allocate more
	size += alignment;

	// Size increment is zero if already reach limit
	// Allow branchless code + do not increase atomic counter anymore.
	// Do not care about counter accuracy here, just in or out.
	size >>= ((m_currPtrAtomic >= m_endPtr)<<5); // Shift by 0 or 32 

	// Perform increment FIRST.
	u8* before = (u8*)ATOMICINCREMENTPTR(&m_currPtrAtomic,size);

	u32 alignmentMiss = ((u32)before) & (alignment - 1);
	alignmentMiss = (alignment - alignmentMiss); // We already over allocated anyway, avoid multiply to restrict.

	if (before + size <= m_endPtr) {
		return &before[alignmentMiss];
	} else {
		return NULL;
	}
}

void  StackAllocator::freeStack		(void*) {
	// Do nothing, never free !
}

/*virtual*/
const IAllocator::Status* StackAllocator::getStatus() {
	m_internalStatus.m_memoryAvailable = m_currPtr - m_basePtr;
	return &m_internalStatus;
}

void StackAllocator::enableMultithreadSupport	(bool enabled) {
	if (enabled) {
		m_allocateFunc = (IAllocator::__allocate)&StackAllocator::allocateStackMT;
	} else {
		m_allocateFunc = (IAllocator::__allocate)&StackAllocator::allocateStack;
	}
}

// =================================================

// =================================================
//  TODO Trash allocator
// =================================================


// =================================================


//=========================================================================================
//  Standard Pool allocator, support multithreading.
//=========================================================================================
bool IsPower2(u32 x)
{
	return ( (x > 0) && ((x & (x - 1)) == 0) );
}

PoolAllocator::PoolAllocator(void* baseMemory, u32 elementSize, u32 elementCount, u32 alignment, bool enableMT)
{
	if (alignment < DEFAULT_ALIGN) {
		alignment = DEFAULT_ALIGN;
	}

	//
	// Our pool use the fact that start == end pointer to find that it is full.
	// Thus, an empty pool can not use start and end at same position.
	// So we put "free" at the end and create a gap of 1 item.
	// This simplifies a lot the code for alloc.
	// But in exchange we loose ONE slot, so internally we add one more slot
	// to make sure that the user has the number of slot that was requested
	// and not elementCount-1 slots.
	//
	elementCount++;

	u32 size = ((elementSize + (alignment-1)) / alignment) * alignment;

	u8* bufferItem	= (u8*)baseMemory;
	
	void** elementPtr	= (void**)&bufferItem[size * ((u64)elementCount)];

	for (u32 n = 0; n < elementCount; n++) {
		elementPtr[n] = &bufferItem[size * ((u64)n)];
	}

	m_internalStatus.m_features		= 0;
	m_internalStatus.m_totalMemory	= ((u64)size) * elementCount;

	if (enableMT) {
		CREATELOCK(&m_lock);
		info.mt.m_elementPtr	= elementPtr;
		info.mt.m_flagAlloc		= false;
		info.mt.m_flagFree		= false;
		info.mt.m_allocMT		= 0;
		info.mt.m_freeMT		= elementCount-1;
		if (IsPower2(elementCount)) {
			info.mt.m_size	= elementCount - 1;
			m_allocateFunc	= (IAllocator::__allocate)	&PoolAllocator::allocatePoolMTPOW2;
			m_freeFunc		= (IAllocator::__free)		&PoolAllocator::freePoolMTPOW2;
		} else {
			info.mt.m_size	= elementCount;
			m_allocateFunc	= (IAllocator::__allocate)	&PoolAllocator::allocatePoolMT;
			m_freeFunc		= (IAllocator::__free)		&PoolAllocator::freePoolMT;
		}
	} else {
		info.st.m_elementPtr	= elementPtr;
		info.st.m_elementEnd	= &elementPtr[elementCount];
		info.st.m_free			= info.st.m_elementEnd - 1;
		info.st.m_alloc			= elementPtr;
		m_allocateFunc			= (IAllocator::__allocate)	&PoolAllocator::allocatePool;
		m_freeFunc				= (IAllocator::__free)		&PoolAllocator::freePool;
	}
}

PoolAllocator::~PoolAllocator() {
	if (m_allocateFunc == &PoolAllocator::allocatePoolMT) {
		DESTROYLOCK(&m_lock);
	}
}


/*static*/
u64 PoolAllocator::getMemoryAmount(u32 elementSize, u32 elementCount, u32 alignment) {
	if (alignment < sizeof(void*)) {
		alignment = sizeof(void*); // Do not want any problem when storing the pointers.
	}

	// See Pool allocator constructor comment about same logic.
	elementCount++;

	u64 size = ((u64)((elementSize + (alignment-1)) / alignment)) * alignment;
	return (elementCount * size) + (sizeof(void*) * elementCount);
}

void* PoolAllocator::allocatePool	(u32 /*size*/, u32 /*alignment*/) {
	if (info.st.m_alloc != info.st.m_free) {
		void* res = *info.st.m_alloc++;
		if (info.st.m_alloc >= info.st.m_elementEnd) { info.st.m_alloc = info.st.m_elementPtr; }
		return res;
	} else {
		return NULL;
	}
}

void  PoolAllocator::freePool		(void* ptr) {
	if (ptr) {
		*info.st.m_free++ = ptr;
		if (info.st.m_free >= info.st.m_elementEnd) { info.st.m_free = info.st.m_elementPtr; }
	}
}

void PoolAllocator::resetCounterMT() {
	LOCK(&m_lock);
	u32 sub = ((0x80000000 / info.mt.m_size) * info.mt.m_size);
	info.mt.m_allocMT -= sub;
	info.mt.m_freeMT  -= sub;
	info.mt.m_flagFree = false;
	info.mt.m_flagAlloc= false;
	UNLOCK(&m_lock);
}

void* PoolAllocator::allocatePoolMT	(u32 /*size*/, u32 /*alignment*/) {
	if (info.mt.m_flagFree && info.mt.m_flagAlloc) { // Prefer now volatile read to "atomic and", facilitate portability
		resetCounterMT();
	}

	u32 alloc;
	u32 diff = info.mt.m_freeMT - info.mt.m_allocMT;
	// if more than 16 core at the SAME time trying to allocate or free !? --> FAIL !
	void* res;
	if (diff > 16) {
		alloc = ATOMICINCREMENT32(&info.mt.m_allocMT, 1);
		res = info.mt.m_elementPtr[alloc % info.mt.m_size];
	} else {
		LOCK(&m_lock);
		if ((alloc = info.mt.m_allocMT) != info.mt.m_freeMT) {
			info.mt.m_allocMT = alloc+1;
			res = info.mt.m_elementPtr[alloc % info.mt.m_size];
		} else {
			res = NULL;
		}
		UNLOCK(&m_lock);
	}

	// We never want alloc to loop and fuck the modulo
	if (alloc & 0x80000000) {
		info.mt.m_flagAlloc = true;
	}

	return res;
}

void  PoolAllocator::freePoolMT		(void* ptr) {
	if (!ptr) { return; }

	if (info.mt.m_flagFree && info.mt.m_flagAlloc) { // Prefer now volatile read to "atomic and", facilitate portability
		resetCounterMT();
	}

	u32 free;
	u32 diff = info.mt.m_freeMT - info.mt.m_allocMT;
	// if more than 16 core at the SAME time trying to allocate or free !? --> FAIL !
	void* res = NULL;
	if (diff > 16) {
		free = ATOMICINCREMENT32(&info.mt.m_freeMT, 1);
		info.mt.m_elementPtr[free % info.mt.m_size] = ptr;
	} else {
		LOCK(&m_lock);
		free = info.mt.m_freeMT;
		info.mt.m_elementPtr[free % info.mt.m_size] = ptr;
		info.mt.m_freeMT = free+1;
		UNLOCK(&m_lock);
	}

	// We never want alloc to loop and fuck the modulo
	if (free & 0x80000000) {
		info.mt.m_flagAlloc = true;
	}
}

void* PoolAllocator::allocatePoolMTPOW2	(u32 /*size*/, u32 /*alignment*/) {
	u32 alloc;
	u32 diff = info.mt.m_freeMT - info.mt.m_allocMT;
	// if more than 16 core at the SAME time trying to allocate or free !? --> FAIL !
	void* res;
	if (diff > 16) {
		alloc = ATOMICINCREMENT32(&info.mt.m_allocMT, 1);
		res = info.mt.m_elementPtr[alloc & info.mt.m_size];
	} else {
		LOCK(&m_lock);
		if ((alloc = info.mt.m_allocMT) != info.mt.m_freeMT) {
			res = info.mt.m_elementPtr[alloc & info.mt.m_size];
			info.mt.m_allocMT = alloc + 1;
		} else {
			res = NULL;				
		}
		UNLOCK(&m_lock);
	}

	return res;
}

void  PoolAllocator::freePoolMTPOW2(void* ptr) {
	if (!ptr) { return; }

	u32 free;
	u32 diff = info.mt.m_freeMT - info.mt.m_allocMT;
	// if more than 16 core at the SAME time trying to allocate or free !? --> FAIL !
	void* res = NULL;
	if (diff > 16) {
		free = ATOMICINCREMENT32(&info.mt.m_freeMT, 1);
		info.mt.m_elementPtr[free & info.mt.m_size] = ptr;
	} else {
		LOCK(&m_lock);
		free = info.mt.m_freeMT;
		info.mt.m_elementPtr[free & info.mt.m_size] = ptr;
		info.mt.m_freeMT = free + 1;
		UNLOCK(&m_lock);
	}
}

/*virtual*/
const IAllocator::Status* PoolAllocator::getStatus() {
	if  (m_allocateFunc == &PoolAllocator::allocatePoolMT) {

		// TODO

	} else {

		// TODO

	}
	return &m_internalStatus;
}
//=========================================================================================

//=========================================================================================
//  Standard Malloc, support multithreading by default.
//	A very simple allocator that uses malloc and free.
//=========================================================================================
void* StandardAllocator::allocateStd	(u32 size, u32 alignment) {
	if (alignment <= sizeof(int)) {
		return std::malloc(size);
	} else {
		return _aligned_malloc(size, alignment);
	}
}

void  StandardAllocator::freeStd		(void* ptr) {
	// call to free without std point to OUR function name.
	std::free(ptr);
}
//=========================================================================================
