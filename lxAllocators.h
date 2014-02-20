#ifndef LX_ALLOCATORS_H
#define LX_ALLOCATORS_H

/*
	Library for allocators.
	======================

	By providing various allocators respecting the same interface,
	we can provide allocation mecanism that are specialized and efficient for their purpose.

	We provide by default the following basic features :

	- Standard Allocator	: wrap system malloc/free
	- Stack Allocator		: own a memory block and just increase at each malloc, never free. Always growing.
	- TrashRing Allocator	: same as stack allocator, except that it loops at the end of the buffer and overwrite.
	- Pool Allocator		: allow to allocate item only of fixed size.

	1/ User can extend new allocator very easily.

	2/ The concept is to provide extremly minimal overhead, high performance code.
	   Thus limitation have been set to make the allocation as light as possible.
	   See limitation.

	3/ All allocators provide selection for multithreading or single thread support and internal implementation
	   switch to optimal code with ZERO overhead for such feature during usage but all are gathered
	   into a single easy to use interface.

	4/ PLEASE READ the comment describing each allocator, as some specific setup may improve performance.

	Design consideration :
	======================

	- Use C++ function pointer instead of virtual to avoid indirection (cache miss)

	- Try to have all data fit within cache line in whatever configuration (64 bit / 32 bit ptr, OS, ...)
	  -> Use union based on working mode.

	- Using function pointer bring the ability to switch pointer at runtime based on change in policy and thus
	allow to use the most efficient code for a given allocator condition.

	- Use virtual function for the ease of implementation when the function are not frequently called. (status, changing state)
	Basically allocation & free only are relying on function pointer mecanism.

	- Allocator support 64 bit source buffer, but allocation is limited to 32 bit size items.
*/

#include "lxTypes.h"

// Need to include here because we need size of LockType (could be a struct depending on platform)
#include "lxPlatformLock.h"	

namespace lx {


#define DEFAULT_ALIGN	(sizeof(void*))

/** Base Allocator Interface. */
class IAllocator {
	//
	// Use C++ for context creation and sub classes, use C like C++ function pointer for performance
	// instead of VTable (save on indirection -> cache miss)
	// and still keep code clean for inheritance.
	//
public:
	struct Status {
		static const u32	SUPPORT_GC				= 1;
		static const u64	UNAVAILABLE				= 0xFFFFFFFF;

		u64 m_totalMemory;
		u64	m_memoryAvailable;
		u32 m_activeMallocCount;
		u32 m_features;
	};

	IAllocator() {
		// By default, if not property init, allocator does nothing.
		m_allocateFunc = (IAllocator::__allocate)&IAllocator::DoNothingAlloc;
		m_freeFunc     = (IAllocator::__free    )&IAllocator::DoNothingFree;

		m_internalStatus.m_activeMallocCount	= Status::UNAVAILABLE;
		m_internalStatus.m_features				= Status::UNAVAILABLE;
		m_internalStatus.m_memoryAvailable		= Status::UNAVAILABLE;
		m_internalStatus.m_totalMemory			= Status::UNAVAILABLE;
	}

	inline
	void* allocate	(u32 size, u32 alignment = DEFAULT_ALIGN) {
		return (*this.*m_allocateFunc)(size,alignment);
	}

	inline
	void  free		(void* ptr) {
		return (*this.*m_freeFunc)(ptr);
	}

	virtual const Status* getStatus() 
	{
		// virtual implementation allows us to do computation instead of maintaining things
		// in free or alloc during each allocation.
		return &m_internalStatus;
	}
protected:
	typedef void*	(IAllocator::*__allocate)		(u32 size, u32 alignment);
	typedef void	(IAllocator::*__free)			(void* ptr);
private:
	void*			DoNothingAlloc					(u32 size, u32 alignment)	{ return 0; }
	void			DoNothingFree					(void* ptr)					{ }

protected:
	// Order by "useness" for cache line (most used at top, close to next cache line where each allocator data is more important)
	Status			m_internalStatus;	// 24 byte
	LockType		m_lock;				// 24 - 40 on Windows x86 - x64
	// May need to put some padding here. sizeof(LockType) + VTable cost...
	__allocate		m_allocateFunc;		// 28 - 48
	__free			m_freeFunc;			// 32 - 56

};


/**	A very simple allocator that uses malloc and free. 
	As malloc and free support multithreading by default, this allocator does always.
	Of course fully 64 bit, no limitation except OS. */
class StandardAllocator : public IAllocator {
public:
	StandardAllocator::StandardAllocator() {
		m_allocateFunc = (IAllocator::__allocate)&StandardAllocator::allocateStd;
		m_freeFunc     = (IAllocator::__free    )&StandardAllocator::freeStd;
		m_internalStatus.m_activeMallocCount	= Status::UNAVAILABLE;
		m_internalStatus.m_features				= 0;
		m_internalStatus.m_memoryAvailable		= Status::UNAVAILABLE;
		m_internalStatus.m_totalMemory			= Status::UNAVAILABLE;
	}
private:
	void* allocateStd	(u32 size, u32 alignment = DEFAULT_ALIGN);
	void  freeStd		(void*);
};


/**	A very simple allocator that NEVER disallocate
	and just pile up allocation until full.
	Very efficient for local temporary work. 
	Note that allocation overhead in multithreading when performing alignement is higher.
	(allocated size = size + alignment)
	If ALL your allocations are using same standard alignment (ex. pointer size) then pass 0
	as alignement IN MULTITHREADING MODE ONLY.
	Support full 64 bit memory space. */
class StackAllocator : public IAllocator {
public:
	StackAllocator(void* baseMemoryStartIncluded, void* baseMemoryEndExcluded, bool enableMT = false)
	:m_basePtr((unsigned char*)baseMemoryStartIncluded)
	{
		m_currPtr		= m_basePtr;
		m_currPtrAtomic	= m_currPtr;
		m_endPtr		= (unsigned char*)baseMemoryEndExcluded;

		m_internalStatus.m_features				= 0;
		m_internalStatus.m_totalMemory			= m_endPtr - m_basePtr;

		m_freeFunc     = (IAllocator::__free    )&StackAllocator::freeStack;
		enableMultithreadSupport(enableMT);
	}

	inline void reset() { m_currPtr = m_basePtr; m_currPtrAtomic	= m_currPtr; }

	virtual const Status* getStatus();
private:
	unsigned char*	m_basePtr;
	unsigned char*	m_currPtr;
	volatile unsigned char* m_currPtrAtomic;
	unsigned char*	m_endPtr;

	void* allocateStack		(u32 size, u32 alignment = DEFAULT_ALIGN);
	void* allocateStackMT	(u32 size, u32 alignment = DEFAULT_ALIGN);
	void  freeStack			(void*);
	void enableMultithreadSupport(bool enabled);
};

/**	A variation of the StackAllocator,
	except the allocation buffer is allowed to loop and overwrite previously allocated
	old data.

	User can set a point where the allocator must NOT overwrite
	to ensure that looping will not trash the data 
	accidentally.

	In this case, allocator will return NULL value. */
class TrashRingAllocator : public IAllocator {
public:
	TrashRingAllocator(void* baseMemory, u32 size, bool enableMT = false)
	:m_basePtr((unsigned char*)baseMemory)
	{
		m_currPtr = m_basePtr;
		m_endPtr  = &m_currPtr[size];

		m_allocateFunc = (IAllocator::__allocate)&TrashRingAllocator::allocateStack;
		m_freeFunc     = (IAllocator::__free    )&TrashRingAllocator::freeStack;
	}

	/** Define point at current allocation*/
	void setStartPoint();
private:
	unsigned char*	m_basePtr;
	unsigned char*	m_currPtr;
	unsigned char*	m_endPtr;

	void* allocateStack	(u32 size, u32 alignment = DEFAULT_ALIGN);
	void  freeStack		(void*);
};

/**	An efficient pool allocator.

    Support lockless multithreading as long as there is not much memory pressure.
	(More than 16 item remaining in the pool and garantee to be safe as long as there is less than 
	16 thread accessing the allocator at the same time) 
	
	For CPU without an integer division instruction, or if the division cost and related logic is too high,
	please use a 2^n size for the pool item count. Allocation IS faster.

	WARNING : Size and alignment are ignored when calling alloc(), always return a fixed size block.
	NOTE    : Overhead is one pointer per element.(seperate memory space)
	Support 64 bit base buffer.
 */
class PoolAllocator : public IAllocator {
public:
	PoolAllocator(void* baseMemory, u32 elementSize, u32 elementCount, u32 alignment, bool enableMT = false);
	~PoolAllocator();
	virtual const Status* getStatus();

	static u64 getMemoryAmount(u32 elementSize, u32 elementCount, u32 alignment);
private:
	struct ST {
		// Single thread
		void**	m_alloc;
		void**	m_free;
		void**	m_elementPtr;
		void**	m_elementEnd;
	};

	struct MT {
		// Multi thread
		void**				m_elementPtr;
		u32					m_size;
		volatile	u32		m_allocMT;
		volatile	u32		m_freeMT;
		volatile	bool	m_flagAlloc;
		volatile	bool	m_flagFree;
	};

	union Info {
		ST st;
		MT mt;
	};
	Info	info; // Data more compact for cache line efficiency using union.

	void* allocatePool		(u32 size, u32 alignment = DEFAULT_ALIGN);
	void  freePool			(void*);

	void* allocatePoolMT	(u32 size, u32 alignment = DEFAULT_ALIGN);
	void  freePoolMT		(void*);
	void  resetCounterMT	();

	void* allocatePoolMTPOW2(u32 size, u32 alignment = DEFAULT_ALIGN);
	void  freePoolMTPOW2	(void*);
};

}

#endif
