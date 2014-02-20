#ifndef LX_TYPES_H
#define LX_TYPES_H

typedef unsigned long long	u64;
typedef int					s32;
typedef unsigned int		u32;
typedef unsigned char		u8;

#define	lxAssert(cond,msg)	{ if (!(cond)) { printf(msg); while (1) { } } }

#endif
