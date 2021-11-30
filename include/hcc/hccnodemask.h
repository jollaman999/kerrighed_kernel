#ifndef __HCC_NODEMASK_H
#define __HCC_NODEMASK_H

/*
 * This file is nearly a copy/paste of linux/cpumask.h (2.6.20)
 * Btw this code is very closed to the NUMA related code linux/nodemask.h
 * It will be a good idea to check if can merge these two files
 *
 * nodemasks provide a bitmap suitable for representing the
 * set of nodes in a system, one bit position per node number.
 *
 * See detailed comments in the file linux/bitmap.h describing the
 * data type on which these hccnodemasks are based.
 *
 * For details of hccnodemask_scnprintf() and hccnodemask_parse_user(),
 * see bitmap_scnprintf() and bitmap_parse_user() in lib/bitmap.c.
 * For details of hccnodelist_scnprintf() and hccnodelist_parse(), see
 * bitmap_scnlistprintf() and bitmap_parselist(), also in bitmap.c.
 * For details of hccnode_remap(), see bitmap_bitremap in lib/bitmap.c
 * For details of hccnodes_remap(), see bitmap_remap in lib/bitmap.c.
 *
 * The available nodemask operations are:
 *
 * void hccnode_set(node, mask)		turn on bit 'node' in mask
 * void hccnode_clear(node, mask)		turn off bit 'node' in mask
 * void hccnodes_setall(mask)		set all bits
 * void hccnodes_clear(mask)		clear all bits
 * int hccnode_isset(node, mask)		true iff bit 'node' set in mask
 * int hccnode_test_and_set(node, mask)	test and set bit 'node' in mask
 *
 * void hccnodes_and(dst, src1, src2)	dst = src1 & src2  [intersection]
 * void hccnodes_or(dst, src1, src2)	dst = src1 | src2  [union]
 * void hccnodes_xor(dst, src1, src2)	dst = src1 ^ src2
 * void hccnodes_andnot(dst, src1, src2)	dst = src1 & ~src2
 * void hccnodes_complement(dst, src)	dst = ~src
 *
 * int hccnodes_equal(mask1, mask2)		Does mask1 == mask2?
 * int hccnodes_intersects(mask1, mask2)	Do mask1 and mask2 intersect?
 * int hccnodes_subset(mask1, mask2)	Is mask1 a subset of mask2?
 * int hccnodes_empty(mask)			Is mask empty (no bits sets)?
 * int hccnodes_full(mask)			Is mask full (all bits sets)?
 * int hccnodes_weight(mask)		Hamming weigh - number of set bits
 *
 * void hccnodes_shift_right(dst, src, n)	Shift right
 * void hccnodes_shift_left(dst, src, n)	Shift left
 *
 * int first_hccnode(mask)			Number lowest set bit, or HCC_MAX_NODES
 * int next_hccnode(node, mask)		Next node past 'node', or HCC_MAX_NODES
 *
 * hccnodemask_t hccnodemask_of_node(node)	Return nodemask with bit 'node' set
 * HCCNODE_MASK_ALL				Initializer - all bits set
 * HCCNODE_MASK_NONE			Initializer - no bits set
 * unsigned long *hccnodes_addr(mask)	Array of unsigned long's in mask
 *
 * int hccnodemask_scnprintf(buf, len, mask) Format nodemask for printing
 * int hccnodemask_parse_user(ubuf, ulen, mask)	Parse ascii string as nodemask
 * int hccnodelist_scnprintf(buf, len, mask) Format nodemask as list for printing
 * int hccnodelist_parse(buf, map)		Parse ascii string as nodelist
 * int hccnode_remap(oldbit, old, new)	newbit = map(old, new)(oldbit)
 * int hccnodes_remap(dst, src, old, new)	*dst = map(old, new)(src)
 *
 * for_each_hccnode_mask(node, mask)		for-loop node over mask
 *
 * int num_online_hccnodes()		Number of online NODEs
 * int num_possible_hccnodes()		Number of all possible NODEs
 * int num_present_hccnodes()		Number of present NODEs
 *
 * int hccnode_online(node)			Is some node online?
 * int hccnode_possible(node)		Is some node possible?
 * int hccnode_present(node)			Is some node present (can schedule)?
 *
 * int any_online_hccnode(mask)		First online node in mask
 *
 * for_each_possible_hccnode(node)		for-loop node over node_possible_map
 * for_each_online_hccnode(node)		for-loop node over node_online_map
 * for_each_present_hccnode(node)		for-loop node over node_present_map
 *
 * Subtlety:
 * 1) The 'type-checked' form of node_isset() causes gcc (3.3.2, anyway)
 *    to generate slightly worse code.  Note for example the additional
 *    40 lines of assembly code compiling the "for each possible node"
 *    loops buried in the disk_stat_read() macros calls when compiling
 *    drivers/block/genhd.c (arch i386, CONFIG_SMP=y).  So use a simple
 *    one-line #define for node_isset(), instead of wrapping an inline
 *    inside a macro, the way we do the other calls.
 */

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/bitmap.h>
#include <hcc/sys/types.h>
#if HCC_MAX_NODES <= 1
/* hcc_node_id is used for some macros in this special case */
#include <hcc/hccinit.h>
#endif


typedef struct { DECLARE_BITMAP(bits, HCC_MAX_NODES); } hccnodemask_t;
typedef struct { DECLARE_BITMAP(bits, HCC_HARD_MAX_NODES); } __hccnodemask_t;

extern hccnodemask_t _unused_hccnodemask_arg_;

#define hccnode_set(node, dst) __hccnode_set((node), &(dst))
static inline void __hccnode_set(int node, volatile hccnodemask_t *dstp)
{
	set_bit(node, dstp->bits);
}

#define hccnode_clear(node, dst) __hccnode_clear((node), &(dst))
static inline void __hccnode_clear(int node, volatile hccnodemask_t *dstp)
{
	clear_bit(node, dstp->bits);
}

#define hccnodes_setall(dst) __hccnodes_setall(&(dst))
static inline void __hccnodes_setall(hccnodemask_t *dstp)
{
	bitmap_fill(dstp->bits, HCC_MAX_NODES);
}

#define hccnodes_clear(dst) __hccnodes_clear(&(dst))
static inline void __hccnodes_clear(hccnodemask_t *dstp)
{
	bitmap_zero(dstp->bits, HCC_MAX_NODES);
}

#define hccnodes_copy(dst, src) __hccnodes_copy(&(dst), &(src))
static inline void __hccnodes_copy(hccnodemask_t *dstp, const hccnodemask_t *srcp)
{
	bitmap_copy(dstp->bits, srcp->bits, HCC_MAX_NODES);
}

/* No static inline type checking - see Subtlety (1) above. */
#define hccnode_isset(node, hccnodemask) test_bit((node), (hccnodemask).bits)
#define __hccnode_isset(node, hccnodemask) test_bit((node), (hccnodemask)->bits)

#define hccnode_test_and_set(node, hccnodemask) __hccnode_test_and_set((node), &(hccnodemask))
static inline int __hccnode_test_and_set(int node, hccnodemask_t *addr)
{
	return test_and_set_bit(node, addr->bits);
}

#define hccnodes_and(dst, src1, src2) __hccnodes_and(&(dst), &(src1), &(src2), HCC_MAX_NODES)
static inline void __hccnodes_and(hccnodemask_t *dstp, const hccnodemask_t *src1p,
					const hccnodemask_t *src2p, int nbits)
{
	bitmap_and(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define hccnodes_or(dst, src1, src2) __hccnodes_or(&(dst), &(src1), &(src2), HCC_MAX_NODES)
static inline void __hccnodes_or(hccnodemask_t *dstp, const hccnodemask_t *src1p,
					const hccnodemask_t *src2p, int nbits)
{
	bitmap_or(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define hccnodes_xor(dst, src1, src2) __hccnodes_xor(&(dst), &(src1), &(src2), HCC_MAX_NODES)
static inline void __hccnodes_xor(hccnodemask_t *dstp, const hccnodemask_t *src1p,
					const hccnodemask_t *src2p, int nbits)
{
	bitmap_xor(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define hccnodes_andnot(dst, src1, src2) \
				__hccnodes_andnot(&(dst), &(src1), &(src2), HCC_MAX_NODES)
static inline void __hccnodes_andnot(hccnodemask_t *dstp, const hccnodemask_t *src1p,
					const hccnodemask_t *src2p, int nbits)
{
	bitmap_andnot(dstp->bits, src1p->bits, src2p->bits, nbits);
}

#define hccnodes_complement(dst, src) __hccnodes_complement(&(dst), &(src), HCC_MAX_NODES)
static inline void __hccnodes_complement(hccnodemask_t *dstp,
					const hccnodemask_t *srcp, int nbits)
{
	bitmap_complement(dstp->bits, srcp->bits, nbits);
}

#define hccnodes_equal(src1, src2) __hccnodes_equal(&(src1), &(src2))
static inline int __hccnodes_equal(const hccnodemask_t *src1p,
				   const hccnodemask_t *src2p)
{
	return bitmap_equal(src1p->bits, src2p->bits, HCC_MAX_NODES);
}

#define hccnodes_intersects(src1, src2) __hccnodes_intersects(&(src1), &(src2), HCC_MAX_NODES)
static inline int __hccnodes_intersects(const hccnodemask_t *src1p,
					const hccnodemask_t *src2p, int nbits)
{
	return bitmap_intersects(src1p->bits, src2p->bits, nbits);
}

#define hccnodes_subset(src1, src2) __hccnodes_subset(&(src1), &(src2), HCC_MAX_NODES)
static inline int __hccnodes_subset(const hccnodemask_t *src1p,
					const hccnodemask_t *src2p, int nbits)
{
	return bitmap_subset(src1p->bits, src2p->bits, nbits);
}

#define hccnodes_empty(src) __hccnodes_empty(&(src))
static inline int __hccnodes_empty(const hccnodemask_t *srcp)
{
	return bitmap_empty(srcp->bits, HCC_MAX_NODES);
}

#define hccnodes_full(nodemask) __hccnodes_full(&(nodemask), HCC_MAX_NODES)
static inline int __hccnodes_full(const hccnodemask_t *srcp, int nbits)
{
	return bitmap_full(srcp->bits, nbits);
}

#define hccnodes_weight(nodemask) __hccnodes_weight(&(nodemask))
static inline int __hccnodes_weight(const hccnodemask_t *srcp)
{
	return bitmap_weight(srcp->bits, HCC_MAX_NODES);
}

#define hccnodes_shift_right(dst, src, n) \
			__hccnodes_shift_right(&(dst), &(src), (n), HCC_MAX_NODES)
static inline void __hccnodes_shift_right(hccnodemask_t *dstp,
					const hccnodemask_t *srcp, int n, int nbits)
{
	bitmap_shift_right(dstp->bits, srcp->bits, n, nbits);
}

#define hccnodes_shift_left(dst, src, n) \
			__hccnodes_shift_left(&(dst), &(src), (n), HCC_MAX_NODES)
static inline void __hccnodes_shift_left(hccnodemask_t *dstp,
					const hccnodemask_t *srcp, int n, int nbits)
{
	bitmap_shift_left(dstp->bits, srcp->bits, n, nbits);
}

#define first_hccnode(src) __first_hccnode(&(src))
static inline int __first_hccnode(const hccnodemask_t *srcp)
{
	return min_t(int, HCC_MAX_NODES, find_first_bit(srcp->bits, HCC_MAX_NODES));
}

#define next_hccnode(n, src) __next_hccnode((n), &(src))
static inline int __next_hccnode(int n, const hccnodemask_t *srcp)
{
	return min_t(int, HCC_MAX_NODES,find_next_bit(srcp->bits, HCC_MAX_NODES, n+1));
}

#define hccnodemask_of_node(node)						\
({									\
	typeof(_unused_hccnodemask_arg_) m;					\
	if (sizeof(m) == sizeof(unsigned long)) {			\
		m.bits[0] = 1UL<<(node);					\
	} else {							\
		hccnodes_clear(m);						\
		hccnode_set((node), m);					\
	}								\
	m;								\
})

#define HCCNODE_MASK_LAST_WORD BITMAP_LAST_WORD_MASK(HCC_MAX_NODES)

#if HCC_MAX_NODES <= BITS_PER_LONG

#define HCCNODE_MASK_ALL							\
(hccnodemask_t) { {								\
	[BITS_TO_LONGS(HCC_MAX_NODES)-1] = HCCNODE_MASK_LAST_WORD			\
} }

#else

#define HCCNODE_MASK_ALL							\
(hccnodemask_t) { {								\
	[0 ... BITS_TO_LONGS(HCC_MAX_NODES)-2] = ~0UL,			\
	[BITS_TO_LONGS(HCC_MAX_NODES)-1] = HCCNODE_MASK_LAST_WORD			\
} }

#endif

#define HCCNODE_MASK_NONE							\
(hccnodemask_t) { {								\
	[0 ... BITS_TO_LONGS(HCC_MAX_NODES)-1] =  0UL				\
} }

#define HCCNODE_MASK_NODE0							\
(hccnodemask_t) { {								\
	[0] =  1UL							\
} }

#define hccnodes_addr(src) ((src).bits)

#define hccnodemask_scnprintf(buf, len, src) \
			__hccnodemask_scnprintf((buf), (len), &(src), HCC_MAX_NODES)
static inline int __hccnodemask_scnprintf(char *buf, int len,
					const hccnodemask_t *srcp, int nbits)
{
	return bitmap_scnprintf(buf, len, srcp->bits, nbits);
}

#define hccnodemask_parse_user(ubuf, ulen, dst) \
			__hccnodemask_parse_user((ubuf), (ulen), &(dst), HCC_MAX_NODES)
static inline int __hccnodemask_parse_user(const char __user *buf, int len,
					hccnodemask_t *dstp, int nbits)
{
	return bitmap_parse_user(buf, len, dstp->bits, nbits);
}

#define hccnodelist_scnprintf(buf, len, src) \
			__hccnodelist_scnprintf((buf), (len), &(src), HCC_MAX_NODES)
static inline int __hccnodelist_scnprintf(char *buf, int len,
					const hccnodemask_t *srcp, int nbits)
{
	return bitmap_scnlistprintf(buf, len, srcp->bits, nbits);
}

#define hccnodelist_parse(buf, dst) __hccnodelist_parse((buf), &(dst), HCC_MAX_NODES)
static inline int __hccnodelist_parse(const char *buf, hccnodemask_t *dstp, int nbits)
{
	return bitmap_parselist(buf, dstp->bits, nbits);
}

#define hccnode_remap(oldbit, old, new) \
		__hccnode_remap((oldbit), &(old), &(new), HCC_MAX_NODES)
static inline int __hccnode_remap(int oldbit,
		const hccnodemask_t *oldp, const hccnodemask_t *newp, int nbits)
{
	return bitmap_bitremap(oldbit, oldp->bits, newp->bits, nbits);
}

#define hccnodes_remap(dst, src, old, new) \
		__hccnodes_remap(&(dst), &(src), &(old), &(new), HCC_MAX_NODES)
static inline void __hccnodes_remap(hccnodemask_t *dstp, const hccnodemask_t *srcp,
		const hccnodemask_t *oldp, const hccnodemask_t *newp, int nbits)
{
	bitmap_remap(dstp->bits, srcp->bits, oldp->bits, newp->bits, nbits);
}

#if HCC_MAX_NODES > 1
#define for_each_hccnode_mask(node, mask)		\
	for ((node) = first_hccnode(mask);		\
		(node) < HCC_MAX_NODES;		\
		(node) = next_hccnode((node), (mask)))
#define __for_each_hccnode_mask(node, mask)		\
	for ((node) = __first_hccnode(mask);		\
		(node) < HCC_MAX_NODES;		\
		(node) = __next_hccnode((node), (mask)))

#else /* HCC_MAX_NODES == 1 */
#define for_each_hccnode_mask(node, mask)		\
	for ((node) = hcc_node_id; (node) < (hcc_node_id+1); (node)++, (void)mask)
#define __for_each_hccnode_mask(node, mask)		\
	for ((node) = hcc_node_id; (node) < (hcc_node_id+1); (node)++, (void)mask)
#endif /* HCC_MAX_NODES */

#define next_hccnode_in_ring(node, v) __next_hccnode_in_ring(node, &(v))
static inline hcc_node_t __next_hccnode_in_ring(hcc_node_t node,
						      const hccnodemask_t *v)
{
	hcc_node_t res;
	res = __next_hccnode(node, v);

	if (res < HCC_MAX_NODES)
		return res;

	return __first_hccnode(v);
}

#define nth_hccnode(node, v) __nth_hccnode(node, &(v))
static inline hcc_node_t __nth_hccnode(hcc_node_t node,
					     const hccnodemask_t *v)
{
	hcc_node_t iter;

	iter = __first_hccnode(v);
	while (node > 0) {
		iter = __next_hccnode(iter, v);
		node--;
	}

	return iter;
}

/** Return true if the index is the only one set in the vector */
#define hccnode_is_unique(node, v) __hccnode_is_unique(node, &(v))
static inline int __hccnode_is_unique(hcc_node_t node,
				      const hccnodemask_t *v)
{
  int i;
  
  i = __first_hccnode(v);
  if(i != node) return 0;
  
  i = __next_hccnode(node, v);
  if(i != HCC_MAX_NODES) return 0;
  
  return 1;
}

/*
 * hccnode_online_map: list of nodes available as object injection target
 * hccnode_present_map: list of nodes ready to be added in a cluster
 * hccnode_possible_map: list of nodes that may join the cluster in the future
 */

extern hccnodemask_t hccnode_possible_map;
extern hccnodemask_t hccnode_online_map;
extern hccnodemask_t hccnode_present_map;

#if HCC_MAX_NODES > 1
#define num_online_hccnodes()	hccnodes_weight(hccnode_online_map)
#define num_possible_hccnodes()	hccnodes_weight(hccnode_possible_map)
#define num_present_hccnodes()	hccnodes_weight(hccnode_present_map)
#define hccnode_online(node)	hccnode_isset((node), hccnode_online_map)
#define hccnode_possible(node)	hccnode_isset((node), hccnode_possible_map)
#define hccnode_present(node)	hccnode_isset((node), hccnode_present_map)

#define any_online_hccnode(mask) __any_online_hccnode(&(mask))
int __any_online_hccnode(const hccnodemask_t *mask);

#else

#define num_online_hccnodes()	1
#define num_possible_hccnodes()	1
#define num_present_hccnodes()	1
#define hccnode_online(node)		((node) == hcc_node_id)
#define hccnode_possible(node)	((node) == hcc_node_id)
#define hccnode_present(node)	((node) == hcc_node_id)

#define any_online_hccnode(mask)		hcc_node_id
#endif

#define for_each_possible_hccnode(node)  for_each_hccnode_mask((node), hccnode_possible_map)
#define for_each_online_hccnode(node)  for_each_hccnode_mask((node), hccnode_online_map)
#define for_each_present_hccnode(node) for_each_hccnode_mask((node), hccnode_present_map)

#define set_hccnode_possible(node) hccnode_set(node, hccnode_possible_map)
#define set_hccnode_online(node)   hccnode_set(node, hccnode_online_map)
#define set_hccnode_present(node)  hccnode_set(node, hccnode_present_map)

#define clear_hccnode_possible(node) hccnode_clear(node, hccnode_possible_map)
#define clear_hccnode_online(node)   hccnode_clear(node, hccnode_online_map)
#define clear_hccnode_present(node)  hccnode_clear(node, hccnode_present_map)

#define nth_possible_hccnode(node) nth_hccnode(node, hccnode_possible_map)
#define nth_online_hccnode(node) nth_hccnode(node, hccnode_online_map)
#define nth_present_hccnode(node) nth_hccnode(node, hccnode_present_map)

#define hccnode_next_possible(node) next_hccnode(node, hccnode_possible_map)
#define hccnode_next_online(node) next_hccnode(node, hccnode_online_map)
#define hccnode_next_present(node) next_hccnode(node, hccnode_present_map)

#define hccnode_next_possible_in_ring(node) next_hccnode_in_ring(node, hccnode_possible_map)
#define hccnode_next_online_in_ring(node) next_hccnode_in_ring(node, hccnode_online_map)
#define hccnode_next_present_in_ring(node) next_hccnode_in_ring(node, hccnode_present_map)

#endif /* __HCC_NODEMASK_H */
