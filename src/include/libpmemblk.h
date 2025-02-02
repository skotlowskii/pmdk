/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2014-2023, Intel Corporation */

/*
 * Libpmemblk is deprecated.
 *
 * libpmemblk.h -- definitions of libpmemblk entry points
 *
 * This library provides support for programming with persistent memory (pmem).
 *
 * libpmemblk provides support for arrays of atomically-writable blocks.
 *
 * See libpmemblk(7) for details.
 */

#ifndef LIBPMEMBLK_H
#define LIBPMEMBLK_H 1

#include <sys/types.h>

#define BLK_DEPR_STR "Libpmemblk is deprecated."
#define PMEMBLK_DEPR_ATTR __attribute__((deprecated(BLK_DEPR_STR)))

#ifdef __cplusplus
extern "C" {
#endif

/*
 * opaque type, internal to libpmemblk
 */
typedef struct pmemblk PMEMblkpool;

/*
 * PMEMBLK_MAJOR_VERSION and PMEMBLK_MINOR_VERSION provide the current version
 * of the libpmemblk API as provided by this header file.  Applications can
 * verify that the version available at run-time is compatible with the version
 * used at compile-time by passing these defines to pmemblk_check_version().
 */
#define PMEMBLK_MAJOR_VERSION 1
#define PMEMBLK_MINOR_VERSION 1

PMEMBLK_DEPR_ATTR
const char *pmemblk_check_version(unsigned major_required,
	unsigned minor_required);

/* XXX - unify minimum pool size for both OS-es */

#if defined(__x86_64__) || defined(__M_X64__) || defined(__aarch64__) || \
	defined(__riscv) || defined(__loongarch64)
/* minimum pool size: 16MiB + 4KiB (minimum BTT size + mmap alignment) */
#define PMEMBLK_MIN_POOL ((size_t)((1u << 20) * 16 + (1u << 10) * 8))
#elif defined(__PPC64__)
/* minimum pool size: 16MiB + 128KiB (minimum BTT size + mmap alignment) */
#define PMEMBLK_MIN_POOL ((size_t)((1u << 20) * 16 + (1u << 10) * 128))
#else
#error unable to recognize ISA at compile time
#endif

/*
 * This limit is set arbitrary to incorporate a pool header and required
 * alignment plus supply.
 */
#define PMEMBLK_MIN_PART ((size_t)(1024 * 1024 * 2)) /* 2 MiB */

#define PMEMBLK_MIN_BLK ((size_t)512)

PMEMBLK_DEPR_ATTR
PMEMblkpool *pmemblk_open(const char *path, size_t bsize);

PMEMBLK_DEPR_ATTR
PMEMblkpool *pmemblk_create(const char *path, size_t bsize, size_t poolsize,
	mode_t mode);

PMEMBLK_DEPR_ATTR
int pmemblk_check(const char *path, size_t bsize);

PMEMBLK_DEPR_ATTR
void pmemblk_close(PMEMblkpool *pbp);
PMEMBLK_DEPR_ATTR
size_t pmemblk_bsize(PMEMblkpool *pbp);
PMEMBLK_DEPR_ATTR
size_t pmemblk_nblock(PMEMblkpool *pbp);
PMEMBLK_DEPR_ATTR
int pmemblk_read(PMEMblkpool *pbp, void *buf, long long blockno);
PMEMBLK_DEPR_ATTR
int pmemblk_write(PMEMblkpool *pbp, const void *buf, long long blockno);
PMEMBLK_DEPR_ATTR
int pmemblk_set_zero(PMEMblkpool *pbp, long long blockno);
PMEMBLK_DEPR_ATTR
int pmemblk_set_error(PMEMblkpool *pbp, long long blockno);

/*
 * Passing NULL to pmemblk_set_funcs() tells libpmemblk to continue to use the
 * default for that function.  The replacement functions must not make calls
 * back into libpmemblk.
 */

PMEMBLK_DEPR_ATTR
void pmemblk_set_funcs(
		void *(*malloc_func)(size_t size),
		void (*free_func)(void *ptr),
		void *(*realloc_func)(void *ptr, size_t size),
		char *(*strdup_func)(const char *s));

PMEMBLK_DEPR_ATTR
const char *pmemblk_errormsg(void);

/* DEPRECATED */
PMEMBLK_DEPR_ATTR
int pmemblk_ctl_get(PMEMblkpool *pbp, const char *name, void *arg);
PMEMBLK_DEPR_ATTR
int pmemblk_ctl_set(PMEMblkpool *pbp, const char *name, void *arg);
PMEMBLK_DEPR_ATTR
int pmemblk_ctl_exec(PMEMblkpool *pbp, const char *name, void *arg);

#ifdef __cplusplus
}
#endif
#endif	/* libpmemblk.h */
