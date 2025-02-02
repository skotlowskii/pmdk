// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2014-2023, Intel Corporation */

/*
 * log.c -- log memory pool entry points for libpmem
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

#include "libpmem.h"
#include "libpmemlog.h"
#include "ctl_global.h"

#include "os.h"
#include "set.h"
#include "out.h"
#include "log.h"
#include "mmap.h"
#include "sys_util.h"
#include "util_pmem.h"
#include "valgrind_internal.h"

static const struct pool_attr Log_create_attr = {
		LOG_HDR_SIG,
		LOG_FORMAT_MAJOR,
		LOG_FORMAT_FEAT_DEFAULT,
		{0}, {0}, {0}, {0}, {0}
};

static const struct pool_attr Log_open_attr = {
		LOG_HDR_SIG,
		LOG_FORMAT_MAJOR,
		LOG_FORMAT_FEAT_CHECK,
		{0}, {0}, {0}, {0}, {0}
};

/*
 * log_descr_create -- (internal) create log memory pool descriptor
 */
static void
log_descr_create(PMEMlogpool *plp, size_t poolsize)
{
	LOG(3, "plp %p poolsize %zu", plp, poolsize);

	ASSERTeq(poolsize % Pagesize, 0);

	/* create required metadata */
	plp->start_offset = htole64(roundup(sizeof(*plp),
					LOG_FORMAT_DATA_ALIGN));
	plp->end_offset = htole64(poolsize);
	plp->write_offset = plp->start_offset;

	/* store non-volatile part of pool's descriptor */
	util_persist(plp->is_pmem, &plp->start_offset, 3 * sizeof(uint64_t));
}

/*
 * log_descr_check -- (internal) validate log memory pool descriptor
 */
static int
log_descr_check(PMEMlogpool *plp, size_t poolsize)
{
	LOG(3, "plp %p poolsize %zu", plp, poolsize);

	struct pmemlog hdr = *plp;
	log_convert2h(&hdr);

	if ((hdr.start_offset !=
			roundup(sizeof(*plp), LOG_FORMAT_DATA_ALIGN)) ||
			(hdr.end_offset != poolsize) ||
			(hdr.start_offset > hdr.end_offset)) {
		ERR("wrong start/end offsets "
			"(start: %" PRIu64 " end: %" PRIu64 "), "
			"pool size %zu",
			hdr.start_offset, hdr.end_offset, poolsize);
		errno = EINVAL;
		return -1;
	}

	if ((hdr.write_offset > hdr.end_offset) || (hdr.write_offset <
			hdr.start_offset)) {
		ERR("wrong write offset (start: %" PRIu64 " end: %" PRIu64
			" write: %" PRIu64 ")",
			hdr.start_offset, hdr.end_offset, hdr.write_offset);
		errno = EINVAL;
		return -1;
	}

	LOG(3, "start: %" PRIu64 ", end: %" PRIu64 ", write: %" PRIu64 "",
		hdr.start_offset, hdr.end_offset, hdr.write_offset);

	return 0;
}

/*
 * log_runtime_init -- (internal) initialize log memory pool runtime data
 */
static int
log_runtime_init(PMEMlogpool *plp, int rdonly)
{
	LOG(3, "plp %p rdonly %d", plp, rdonly);

	/* remove volatile part of header */
	VALGRIND_REMOVE_PMEM_MAPPING(&plp->addr,
		sizeof(struct pmemlog) -
		sizeof(struct pool_hdr) -
		3 * sizeof(uint64_t));

	/*
	 * Use some of the memory pool area for run-time info.  This
	 * run-time state is never loaded from the file, it is always
	 * created here, so no need to worry about byte-order.
	 */
	plp->rdonly = rdonly;

	if ((plp->rwlockp = Malloc(sizeof(*plp->rwlockp))) == NULL) {
		ERR("!Malloc for a RW lock");
		return -1;
	}

	util_rwlock_init(plp->rwlockp);

	/*
	 * If possible, turn off all permissions on the pool header page.
	 *
	 * The prototype PMFS doesn't allow this when large pages are in
	 * use. It is not considered an error if this fails.
	 */
	RANGE_NONE(plp->addr, sizeof(struct pool_hdr), plp->is_dev_dax);

	/* the rest should be kept read-only (debug version only) */
	RANGE_RO((char *)plp->addr + sizeof(struct pool_hdr),
			plp->size - sizeof(struct pool_hdr), plp->is_dev_dax);

	return 0;
}

/*
 * pmemlog_createU -- create a log memory pool
 */
static inline
PMEMlogpool *
pmemlog_createU(const char *path, size_t poolsize, mode_t mode)
{
	LOG(3, "path %s poolsize %zu mode %d", path, poolsize, mode);

	struct pool_set *set;
	struct pool_attr adj_pool_attr = Log_create_attr;

	/* force set SDS feature */
	if (SDS_at_create)
		adj_pool_attr.features.incompat |= POOL_FEAT_SDS;
	else
		adj_pool_attr.features.incompat &= ~POOL_FEAT_SDS;

	if (util_pool_create(&set, path, poolsize, PMEMLOG_MIN_POOL,
			PMEMLOG_MIN_PART, &adj_pool_attr, NULL,
			REPLICAS_DISABLED) != 0) {
		LOG(2, "cannot create pool or pool set");
		return NULL;
	}

	ASSERT(set->nreplicas > 0);

	struct pool_replica *rep = set->replica[0];
	PMEMlogpool *plp = rep->part[0].addr;

	VALGRIND_REMOVE_PMEM_MAPPING(&plp->addr,
			sizeof(struct pmemlog) -
			((uintptr_t)&plp->addr - (uintptr_t)&plp->hdr));

	plp->addr = plp;
	plp->size = rep->repsize;
	plp->set = set;
	plp->is_pmem = rep->is_pmem;
	plp->is_dev_dax = rep->part[0].is_dev_dax;

	/* is_dev_dax implies is_pmem */
	ASSERT(!plp->is_dev_dax || plp->is_pmem);

	/* create pool descriptor */
	log_descr_create(plp, rep->repsize);

	/* initialize runtime parts */
	if (log_runtime_init(plp, 0) != 0) {
		ERR("pool initialization failed");
		goto err;
	}

	if (util_poolset_chmod(set, mode))
		goto err;

	util_poolset_fdclose(set);

	LOG(3, "plp %p", plp);
	return plp;

err:
	LOG(4, "error clean up");
	int oerrno = errno;
	util_poolset_close(set, DELETE_CREATED_PARTS);
	errno = oerrno;
	return NULL;
}

/*
 * pmemlog_create -- create a log memory pool
 */
PMEMlogpool *
pmemlog_create(const char *path, size_t poolsize, mode_t mode)
{
	return pmemlog_createU(path, poolsize, mode);
}

/*
 * log_open_common -- (internal) open a log memory pool
 *
 * This routine does all the work, but takes a cow flag so internal
 * calls can map a read-only pool if required.
 */
static PMEMlogpool *
log_open_common(const char *path, unsigned flags)
{
	LOG(3, "path %s flags 0x%x", path, flags);

	struct pool_set *set;

	if (util_pool_open(&set, path, PMEMLOG_MIN_PART, &Log_open_attr,
			NULL, NULL, flags) != 0) {
		LOG(2, "cannot open pool or pool set");
		return NULL;
	}

	ASSERT(set->nreplicas > 0);

	struct pool_replica *rep = set->replica[0];
	PMEMlogpool *plp = rep->part[0].addr;

	VALGRIND_REMOVE_PMEM_MAPPING(&plp->addr,
			sizeof(struct pmemlog) -
			((uintptr_t)&plp->addr - (uintptr_t)&plp->hdr));

	plp->addr = plp;
	plp->size = rep->repsize;
	plp->set = set;
	plp->is_pmem = rep->is_pmem;
	plp->is_dev_dax = rep->part[0].is_dev_dax;

	/* is_dev_dax implies is_pmem */
	ASSERT(!plp->is_dev_dax || plp->is_pmem);

	if (set->nreplicas > 1) {
		errno = ENOTSUP;
		ERR("!replicas not supported");
		goto err;
	}

	/* validate pool descriptor */
	if (log_descr_check(plp, rep->repsize) != 0) {
		LOG(2, "descriptor check failed");
		goto err;
	}

	/* initialize runtime parts */
	if (log_runtime_init(plp, set->rdonly) != 0) {
		ERR("pool initialization failed");
		goto err;
	}

	util_poolset_fdclose(set);

	LOG(3, "plp %p", plp);
	return plp;

err:
	LOG(4, "error clean up");
	int oerrno = errno;
	util_poolset_close(set, DO_NOT_DELETE_PARTS);
	errno = oerrno;
	return NULL;
}

/*
 * pmemlog_openU -- open an existing log memory pool
 */
static inline
PMEMlogpool *
pmemlog_openU(const char *path)
{
	LOG(3, "path %s", path);

	return log_open_common(path, COW_at_open ? POOL_OPEN_COW : 0);
}

/*
 * pmemlog_open -- open an existing log memory pool
 */
PMEMlogpool *
pmemlog_open(const char *path)
{
	return pmemlog_openU(path);
}

/*
 * pmemlog_close -- close a log memory pool
 */
void
pmemlog_close(PMEMlogpool *plp)
{
	LOG(3, "plp %p", plp);

	util_rwlock_destroy(plp->rwlockp);
	Free((void *)plp->rwlockp);

	util_poolset_close(plp->set, DO_NOT_DELETE_PARTS);
}

/*
 * pmemlog_nbyte -- return usable size of a log memory pool
 */
size_t
pmemlog_nbyte(PMEMlogpool *plp)
{
	LOG(3, "plp %p", plp);

	util_rwlock_rdlock(plp->rwlockp);

	size_t size = le64toh(plp->end_offset) - le64toh(plp->start_offset);
	LOG(4, "plp %p nbyte %zu", plp, size);

	util_rwlock_unlock(plp->rwlockp);

	return size;
}

/*
 * log_persist -- (internal) persist data, then metadata
 *
 * On entry, the write lock should be held.
 */
static void
log_persist(PMEMlogpool *plp, uint64_t new_write_offset)
{
	uint64_t old_write_offset = le64toh(plp->write_offset);
	size_t length = new_write_offset - old_write_offset;

	/* unprotect the log space range (debug version only) */
	RANGE_RW((char *)plp->addr + old_write_offset, length, plp->is_dev_dax);

	/* persist the data */
	if (plp->is_pmem)
		pmem_drain(); /* data already flushed */
	else
		pmem_msync((char *)plp->addr + old_write_offset, length);

	/* protect the log space range (debug version only) */
	RANGE_RO((char *)plp->addr + old_write_offset, length, plp->is_dev_dax);

	/* unprotect the pool descriptor (debug version only) */
	RANGE_RW((char *)plp->addr + sizeof(struct pool_hdr),
			LOG_FORMAT_DATA_ALIGN, plp->is_dev_dax);

	/* write the metadata */
	plp->write_offset = htole64(new_write_offset);

	/* persist the metadata */
	if (plp->is_pmem)
		pmem_persist(&plp->write_offset, sizeof(plp->write_offset));
	else
		pmem_msync(&plp->write_offset, sizeof(plp->write_offset));

	/* set the write-protection again (debug version only) */
	RANGE_RO((char *)plp->addr + sizeof(struct pool_hdr),
			LOG_FORMAT_DATA_ALIGN, plp->is_dev_dax);
}

/*
 * pmemlog_append -- add data to a log memory pool
 */
int
pmemlog_append(PMEMlogpool *plp, const void *buf, size_t count)
{
	int ret = 0;

	LOG(3, "plp %p buf %p count %zu", plp, buf, count);

	if (plp->rdonly) {
		ERR("can't append to read-only log");
		errno = EROFS;
		return -1;
	}

	util_rwlock_wrlock(plp->rwlockp);

	/* get the current values */
	uint64_t end_offset = le64toh(plp->end_offset);
	uint64_t write_offset = le64toh(plp->write_offset);

	if (write_offset >= end_offset) {
		/* no space left */
		errno = ENOSPC;
		ERR("!pmemlog_append");
		ret = -1;
		goto end;
	}

	/* make sure we don't write past the available space */
	if (count > (end_offset - write_offset)) {
		errno = ENOSPC;
		ERR("!pmemlog_append");
		ret = -1;
		goto end;
	}

	char *data = plp->addr;

	/*
	 * unprotect the log space range, where the new data will be stored
	 * (debug version only)
	 */
	RANGE_RW(&data[write_offset], count, plp->is_dev_dax);

	if (plp->is_pmem)
		pmem_memcpy_nodrain(&data[write_offset], buf, count);
	else
		memcpy(&data[write_offset], buf, count);

	/* protect the log space range (debug version only) */
	RANGE_RO(&data[write_offset], count, plp->is_dev_dax);

	write_offset += count;

	/* persist the data and the metadata */
	log_persist(plp, write_offset);

end:
	util_rwlock_unlock(plp->rwlockp);

	return ret;
}

/*
 * pmemlog_appendv -- add gathered data to a log memory pool
 */
int
pmemlog_appendv(PMEMlogpool *plp, const struct iovec *iov, int iovcnt)
{
	LOG(3, "plp %p iovec %p iovcnt %d", plp, iov, iovcnt);

	int ret = 0;
	int i;

	if (iovcnt < 0) {
		errno = EINVAL;
		ERR("iovcnt is less than zero: %d", iovcnt);
		return -1;
	}

	if (plp->rdonly) {
		ERR("can't append to read-only log");
		errno = EROFS;
		return -1;
	}

	util_rwlock_wrlock(plp->rwlockp);

	/* get the current values */
	uint64_t end_offset = le64toh(plp->end_offset);
	uint64_t write_offset = le64toh(plp->write_offset);

	if (write_offset >= end_offset) {
		/* no space left */
		errno = ENOSPC;
		ERR("!pmemlog_appendv");
		ret = -1;
		goto end;
	}

	char *data = plp->addr;
	uint64_t count = 0;
	char *buf;

	/* calculate required space */
	for (i = 0; i < iovcnt; ++i)
		count += iov[i].iov_len;

	/* check if there is enough free space */
	if (count > (end_offset - write_offset)) {
		errno = ENOSPC;
		ret = -1;
		goto end;
	}

	/* append the data */
	for (i = 0; i < iovcnt; ++i) {
		buf = iov[i].iov_base;
		count = iov[i].iov_len;

		/*
		 * unprotect the log space range, where the new data will be
		 * stored (debug version only)
		 */
		RANGE_RW(&data[write_offset], count, plp->is_dev_dax);

		if (plp->is_pmem)
			pmem_memcpy_nodrain(&data[write_offset], buf, count);
		else
			memcpy(&data[write_offset], buf, count);

		/*
		 * protect the log space range (debug version only)
		 */
		RANGE_RO(&data[write_offset], count, plp->is_dev_dax);

		write_offset += count;
	}

	/* persist the data and the metadata */
	log_persist(plp, write_offset);

end:
	util_rwlock_unlock(plp->rwlockp);

	return ret;
}

/*
 * pmemlog_tell -- return current write point in a log memory pool
 */
long long
pmemlog_tell(PMEMlogpool *plp)
{
	LOG(3, "plp %p", plp);

	util_rwlock_rdlock(plp->rwlockp);

	ASSERT(le64toh(plp->write_offset) >= le64toh(plp->start_offset));
	long long wp = (long long)(le64toh(plp->write_offset) -
			le64toh(plp->start_offset));

	LOG(4, "write offset %lld", wp);

	util_rwlock_unlock(plp->rwlockp);

	return wp;
}

/*
 * pmemlog_rewind -- discard all data, resetting a log memory pool to empty
 */
void
pmemlog_rewind(PMEMlogpool *plp)
{
	LOG(3, "plp %p", plp);

	if (plp->rdonly) {
		ERR("can't rewind read-only log");
		errno = EROFS;
		return;
	}

	util_rwlock_wrlock(plp->rwlockp);

	/* unprotect the pool descriptor (debug version only) */
	RANGE_RW((char *)plp->addr + sizeof(struct pool_hdr),
			LOG_FORMAT_DATA_ALIGN, plp->is_dev_dax);

	plp->write_offset = plp->start_offset;
	if (plp->is_pmem)
		pmem_persist(&plp->write_offset, sizeof(uint64_t));
	else
		pmem_msync(&plp->write_offset, sizeof(uint64_t));

	/* set the write-protection again (debug version only) */
	RANGE_RO((char *)plp->addr + sizeof(struct pool_hdr),
			LOG_FORMAT_DATA_ALIGN, plp->is_dev_dax);

	util_rwlock_unlock(plp->rwlockp);
}

/*
 * pmemlog_walk -- walk through all data in a log memory pool
 *
 * chunksize of 0 means process_chunk gets called once for all data
 * as a single chunk.
 */
void
pmemlog_walk(PMEMlogpool *plp, size_t chunksize,
	int (*process_chunk)(const void *buf, size_t len, void *arg), void *arg)
{
	LOG(3, "plp %p chunksize %zu", plp, chunksize);

	/*
	 * We are assuming that the walker doesn't change the data it's reading
	 * in place. We prevent everyone from changing the data behind our back
	 * until we are done with processing it.
	 */
	util_rwlock_rdlock(plp->rwlockp);

	char *data = plp->addr;
	uint64_t write_offset = le64toh(plp->write_offset);
	uint64_t data_offset = le64toh(plp->start_offset);
	size_t len;

	if (chunksize == 0) {
		/* most common case: process everything at once */
		len = write_offset - data_offset;
		LOG(3, "length %zu", len);
		(*process_chunk)(&data[data_offset], len, arg);
	} else {
		/*
		 * Walk through the complete record, chunk by chunk.
		 * The callback returns 0 to terminate the walk.
		 */
		while (data_offset < write_offset) {
			len = MIN(chunksize, write_offset - data_offset);
			if (!(*process_chunk)(&data[data_offset], len, arg))
				break;
			data_offset += chunksize;
		}
	}

	util_rwlock_unlock(plp->rwlockp);
}

/*
 * pmemlog_checkU -- log memory pool consistency check
 *
 * Returns true if consistent, zero if inconsistent, -1/error if checking
 * cannot happen due to other errors.
 */
static inline
int
pmemlog_checkU(const char *path)
{
	LOG(3, "path \"%s\"", path);

	PMEMlogpool *plp = log_open_common(path, POOL_OPEN_COW);
	if (plp == NULL)
		return -1;	/* errno set by log_open_common() */

	int consistent = 1;

	/* validate pool descriptor */
	uint64_t hdr_start = le64toh(plp->start_offset);
	uint64_t hdr_end = le64toh(plp->end_offset);
	uint64_t hdr_write = le64toh(plp->write_offset);

	if (hdr_start != roundup(sizeof(*plp), LOG_FORMAT_DATA_ALIGN)) {
		ERR("wrong value of start_offset");
		consistent = 0;
	}

	if (hdr_end != plp->size) {
		ERR("wrong value of end_offset");
		consistent = 0;
	}

	if (hdr_start > hdr_end) {
		ERR("start_offset greater than end_offset");
		consistent = 0;
	}

	if (hdr_start > hdr_write) {
		ERR("start_offset greater than write_offset");
		consistent = 0;
	}

	if (hdr_write > hdr_end) {
		ERR("write_offset greater than end_offset");
		consistent = 0;
	}

	pmemlog_close(plp);

	if (consistent)
		LOG(4, "pool consistency check OK");

	return consistent;
}

/*
 * pmemlog_check -- log memory pool consistency check
 *
 * Returns true if consistent, zero if inconsistent, -1/error if checking
 * cannot happen due to other errors.
 */
int
pmemlog_check(const char *path)
{
	return pmemlog_checkU(path);
}

/*
 * pmemlog_ctl_getU -- programmatically executes a read ctl query
 */
static inline
int
pmemlog_ctl_getU(PMEMlogpool *plp, const char *name, void *arg)
{
	LOG(3, "plp %p name %s arg %p", plp, name, arg);
	return ctl_query(plp == NULL ? NULL : plp->ctl, plp,
			CTL_QUERY_PROGRAMMATIC, name, CTL_QUERY_READ, arg);
}

/*
 * pmemblk_ctl_setU -- programmatically executes a write ctl query
 */
static inline
int
pmemlog_ctl_setU(PMEMlogpool *plp, const char *name, void *arg)
{
	LOG(3, "plp %p name %s arg %p", plp, name, arg);
	return ctl_query(plp == NULL ? NULL : plp->ctl, plp,
		CTL_QUERY_PROGRAMMATIC, name, CTL_QUERY_WRITE, arg);
}

/*
 * pmemlog_ctl_execU -- programmatically executes a runnable ctl query
 */
static inline
int
pmemlog_ctl_execU(PMEMlogpool *plp, const char *name, void *arg)
{
	LOG(3, "plp %p name %s arg %p", plp, name, arg);
	return ctl_query(plp == NULL ? NULL : plp->ctl, plp,
		CTL_QUERY_PROGRAMMATIC, name, CTL_QUERY_RUNNABLE, arg);
}

/*
 * pmemlog_ctl_get -- programmatically executes a read ctl query
 */
int
pmemlog_ctl_get(PMEMlogpool *plp, const char *name, void *arg)
{
	return pmemlog_ctl_getU(plp, name, arg);
}

/*
 * pmemlog_ctl_set -- programmatically executes a write ctl query
 */
int
pmemlog_ctl_set(PMEMlogpool *plp, const char *name, void *arg)
{
	return pmemlog_ctl_setU(plp, name, arg);
}

/*
 * pmemlog_ctl_exec -- programmatically executes a runnable ctl query
 */
int
pmemlog_ctl_exec(PMEMlogpool *plp, const char *name, void *arg)
{
	return pmemlog_ctl_execU(plp, name, arg);
}

#if FAULT_INJECTION
void
pmemlog_inject_fault_at(enum pmem_allocation_type type, int nth,
							const char *at)
{
	core_inject_fault_at(type, nth, at);
}

int
pmemlog_fault_injection_enabled(void)
{
	return core_fault_injection_enabled();
}
#endif
