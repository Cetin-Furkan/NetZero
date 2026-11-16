/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Project NetZero v2 — Intel Tiger Lake (Willow Cove, i7-1165G7) Deep-ISA
 * Hardware Primitives & C23 Kernel Acceleration Layer.
 *
 * Exposes Tiger Lake instructions that compilers cannot auto-synthesize under
 * standard kernel flags (-mno-sse -mno-avx):
 *   - Hardware GP-register CRC32C (crc32l / crc32q, 3-cycle Port 1, no FPU)
 *   - BMI1 1-cycle bit-scan & clear (tzcntq / blsrq)
 *   - Core ID fast read via IA32_TSC_AUX (rdpid)
 *   - RFO ownership prefetcher (prefetchw) & L1->L3 clean demotion (clwb)
 *   - Atomic 64-byte direct cacheline transfer (movdir64b)
 *   - Fast Short REP MOVSB (fsrm) & FPU-guarded AVX-512 256B/iter zmm copy
 */
#ifndef _LINUX_NETZERO_TIGERLAKE_H
#define _LINUX_NETZERO_TIGERLAKE_H

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/string.h>
#include <linux/smp.h>

#ifdef CONFIG_X86_64
#include <asm/cpufeature.h>
#include <asm/fpu/api.h>
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define NZ_CONSTEXPR constexpr
#else
#define NZ_CONSTEXPR static const
#endif

#define NZ_CACHELINE_BYTES 64UL
static_assert(NZ_CACHELINE_BYTES == 64UL, "Tiger Lake Willow Cove cacheline must be 64B");

/*
 * 1. Hardware CRC32C (Castagnoli 0x1edc6f41) on GP registers.
 * Executes on Willow Cove integer Port 1 in 3 cycles with ZERO FPU/XMM usage.
 */
static __always_inline u32 nz_crc32c_u32(u32 crc, u32 val)
{
#ifdef CONFIG_X86_64
	if (cpu_feature_enabled(X86_FEATURE_XMM4_2)) {
		asm("crc32l %[val], %[crc]"
		    : [crc] "+r" (crc)
		    : [val] "rm" (val));
		return crc;
	}
#endif
	val ^= crc;
	val *= 0x85ebca6bU;
	val ^= val >> 13;
	return val * 0xc2b2ae35U;
}

static __always_inline u64 nz_crc32c_u64(u64 crc, u64 val)
{
#ifdef CONFIG_X86_64
	if (cpu_feature_enabled(X86_FEATURE_XMM4_2)) {
		asm("crc32q %[val], %[crc]"
		    : [crc] "+r" (crc)
		    : [val] "rm" (val));
		return crc;
	}
#endif
	val ^= crc;
	val *= 0x9e3779b97f4a7c15ULL;
	val ^= val >> 30;
	return val * 0xbf58476d1ce4e5b9ULL;
}

static __always_inline u32 nz_hash_pair64(u64 a, u64 b)
{
	u64 h = nz_crc32c_u64(0x9e3779b9U, a);

	return (u32)nz_crc32c_u64(h, b);
}

/*
 * 2. BMI1 1-cycle lowest-bit clear (BLSR) & trailing-zero count (TZCNT).
 */
static __always_inline unsigned int nz_tzcnt64(u64 v)
{
#ifdef CONFIG_X86_64
	if (cpu_feature_enabled(X86_FEATURE_BMI1)) {
		u64 out;

		asm("tzcntq %[v], %[out]"
		    : [out] "=r" (out)
		    : [v] "rm" (v)
		    : "cc");
		return (unsigned int)out;
	}
#endif
	return v ? (unsigned int)__ffs64(v) : 64U;
}

static __always_inline u64 nz_blsr64(u64 v)
{
#ifdef CONFIG_X86_64
	if (cpu_feature_enabled(X86_FEATURE_BMI1)) {
		u64 out;

		asm("blsrq %[v], %[out]"
		    : [out] "=r" (out)
		    : [v] "rm" (v)
		    : "cc");
		return out;
	}
#endif
	return v & (v - 1ULL);
}

/*
 * 3. Fast processor ID lookup via Tiger Lake RDPID (IA32_TSC_AUX & 0xfff).
 */
static __always_inline unsigned int nz_rdpid_cpu(void)
{
#ifdef CONFIG_X86_64
	if (cpu_feature_enabled(X86_FEATURE_RDPID)) {
		u64 aux;

		/* rdpid %rax encoded cleanly for universal gas/LLVM support */
		asm volatile("rdpid %[aux]" : [aux] "=r" (aux));
		return (unsigned int)(aux & 0xfffU);
	}
#endif
	return raw_smp_processor_id();
}

/*
 * 4. Willow Cove Cache Hierarchy Coherency Hints:
 *    - nz_prefetchw_range(): RFO pre-ownership into 48 KiB L1D before writes
 *    - nz_clwb_range(): Non-invalidating writeback to shared 12 MiB L3 fabric
 *      so cross-core consumers avoid L1D snoop stalls on >= 32 KiB IPC bursts.
 */
static __always_inline void nz_prefetchw_range(void *dst, size_t len)
{
#ifdef CONFIG_X86_64
	u8 *p = (u8 *)dst;
	size_t span = len > 384 ? 384 : len;
	size_t off;

	for (off = 0; off < span; off += NZ_CACHELINE_BYTES)
		asm volatile("prefetchw (%[addr])" :: [addr] "r" (p + off) : "memory");
#else
	(void)dst;
	(void)len;
#endif
}

static __always_inline void nz_clwb_range(const void *addr, size_t len)
{
#ifdef CONFIG_X86_64
	if (cpu_feature_enabled(X86_FEATURE_CLWB)) {
		const u8 *p = (const u8 *)((uintptr_t)addr & ~(NZ_CACHELINE_BYTES - 1UL));
		const u8 *end = (const u8 *)addr + len;

		for (; p < end; p += NZ_CACHELINE_BYTES)
			asm volatile("clwb (%[addr])" :: [addr] "r" (p) : "memory");
	}
#else
	(void)addr;
	(void)len;
#endif
}

/*
 * 5. Tiger Lake MOVDIR64B: single-instruction atomic 64-byte cacheline move
 *    (destination must be 64-byte aligned).
 */
static __always_inline void nz_movdir64b(void *dst64, const void *src64)
{
#ifdef CONFIG_X86_64
	if (cpu_feature_enabled(X86_FEATURE_MOVDIR64B) &&
	    likely(((uintptr_t)dst64 & 63UL) == 0)) {
		asm volatile("movdir64b (%[src]), %[dst]"
			     :
			     : [dst] "r" (dst64), [src] "r" (src64)
			     : "memory");
		return;
	}
#endif
	memcpy(dst64, src64, 64);
}

/*
 * 6. Tiger Lake FSRM (Fast Short REP MOVSB) + FPU-guarded AVX-512 256B/loop
 *    bulk kernel memory mover (used for live lockless ring upgrades & staging).
 */
static __always_inline void nz_fsrm_kcopy(void *dst, const void *src, size_t len)
{
#ifdef CONFIG_X86_64
	if (cpu_feature_enabled(X86_FEATURE_FSRM)) {
		void *d = dst;
		const void *s = src;
		size_t n = len;

		asm volatile("rep; movsb"
			     : "+D" (d), "+S" (s), "+c" (n)
			     :
			     : "memory");
		return;
	}
#endif
	memcpy(dst, src, len);
}

static inline void nz_avx512_kcopy(void *dst, const void *src, size_t len)
{
#ifdef CONFIG_X86_64
	if (len >= 512 &&
	    cpu_feature_enabled(X86_FEATURE_AVX512F) &&
	    irq_fpu_usable()) {
		u8 *d = (u8 *)dst;
		const u8 *s = (const u8 *)src;
		size_t blocks = len >> 8; /* 256 bytes (4 x zmm) per iteration */

		kernel_fpu_begin();
		while (blocks--) {
			asm volatile(
				".arch generic64+avx512f\n\t"
				"vmovdqu64 0(%[s]), %%zmm0\n\t"
				"vmovdqu64 64(%[s]), %%zmm1\n\t"
				"vmovdqu64 128(%[s]), %%zmm2\n\t"
				"vmovdqu64 192(%[s]), %%zmm3\n\t"
				"vmovdqu64 %%zmm0, 0(%[d])\n\t"
				"vmovdqu64 %%zmm1, 64(%[d])\n\t"
				"vmovdqu64 %%zmm2, 128(%[d])\n\t"
				"vmovdqu64 %%zmm3, 192(%[d])\n\t"
				:
				: [d] "r" (d), [s] "r" (s)
				: "memory");
			d += 256;
			s += 256;
		}
		asm volatile(".arch generic64+avx\n\tvzeroupper" ::: "memory");
		kernel_fpu_end();

		len &= 255UL;
		if (len)
			nz_fsrm_kcopy(d, s, len);
		return;
	}
#endif
	if (len)
		nz_fsrm_kcopy(dst, src, len);
}

#endif /* _LINUX_NETZERO_TIGERLAKE_H */
