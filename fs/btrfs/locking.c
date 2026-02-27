#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/spinlock.h>
#include <linux/page-flags.h>
#include <asm/bug.h>
#include "misc.h"
#include "ctree.h"
#include "extent_io.h"
#include "locking.h"

/*
 * Lockdep class keys for extent_buffer->lock's in this root.  For a given
 * eb, the lockdep key is determined by the btrfs_root it belongs to and
 * the level the eb occupies in the tree.
 *
 * Different roots are used for different purposes and may nest inside each
 * other and they require separate keysets.  As lockdep keys should be
 * static, assign keysets according to the purpose of the root as indicated
 * by btrfs_root->root_key.objectid.  This ensures that all special purpose
 * roots have separate keysets.
 *
 * Lock-nesting across peer nodes is always done with the immediate parent
 * node locked thus preventing deadlock.  As lockdep doesn't know this, use
 * subclass to avoid triggering lockdep warning in such cases.
 *
 * The key is set by the readpage_end_io_hook after the buffer has passed
 * csum validation but before the pages are unlocked.  It is also set by
 * btrfs_init_new_buffer on freshly allocated blocks.
 *
 * We also add a check to make sure the highest level of the tree is the
 * same as our lockdep setup here.  If BTRFS_MAX_LEVEL changes, this code
 * needs update as well.
 */
#ifdef CONFIG_DEBUG_LOCK_ALLOC
#if BTRFS_MAX_LEVEL != 8
#error
#endif

#define DEFINE_LEVEL(stem, level)					\
	.names[level] = "btrfs-" stem "-0" #level,

#define DEFINE_NAME(stem)						\
	DEFINE_LEVEL(stem, 0)						\
	DEFINE_LEVEL(stem, 1)						\
	DEFINE_LEVEL(stem, 2)						\
	DEFINE_LEVEL(stem, 3)						\
	DEFINE_LEVEL(stem, 4)						\
	DEFINE_LEVEL(stem, 5)						\
	DEFINE_LEVEL(stem, 6)						\
	DEFINE_LEVEL(stem, 7)

static struct btrfs_lockdep_keyset {
	u64			id;		/* root objectid */
#ifdef MY_ABC_HERE
	/* Longest entry: btrfs-block-group-cache-00 */
	char			names[BTRFS_MAX_LEVEL][27];
#else /* MY_ABC_HERE */
	/* Longest entry: btrfs-free-space-00 */
	char			names[BTRFS_MAX_LEVEL][20];
#endif /* MY_ABC_HERE */
	struct lock_class_key	keys[BTRFS_MAX_LEVEL];
} btrfs_lockdep_keysets[] = {
	{ .id = BTRFS_ROOT_TREE_OBJECTID,	DEFINE_NAME("root")	},
	{ .id = BTRFS_EXTENT_TREE_OBJECTID,	DEFINE_NAME("extent")	},
	{ .id = BTRFS_CHUNK_TREE_OBJECTID,	DEFINE_NAME("chunk")	},
	{ .id = BTRFS_DEV_TREE_OBJECTID,	DEFINE_NAME("dev")	},
	{ .id = BTRFS_CSUM_TREE_OBJECTID,	DEFINE_NAME("csum")	},
	{ .id = BTRFS_QUOTA_TREE_OBJECTID,	DEFINE_NAME("quota")	},
#ifdef MY_ABC_HERE
	{ .id = BTRFS_SYNO_QUOTA_V2_TREE_OBJECTID,	DEFINE_NAME("syno-v2-quota") },
	{ .id = BTRFS_SYNO_USRQUOTA_V2_TREE_OBJECTID,	DEFINE_NAME("syno-v2-usrquota") },
#endif /* MY_ABC_HERE */
	{ .id = BTRFS_TREE_LOG_OBJECTID,	DEFINE_NAME("log")	},
	{ .id = BTRFS_TREE_RELOC_OBJECTID,	DEFINE_NAME("treloc")	},
	{ .id = BTRFS_DATA_RELOC_TREE_OBJECTID,	DEFINE_NAME("dreloc")	},
	{ .id = BTRFS_UUID_TREE_OBJECTID,	DEFINE_NAME("uuid")	},
	{ .id = BTRFS_FREE_SPACE_TREE_OBJECTID,	DEFINE_NAME("free-space") },
#ifdef MY_ABC_HERE
	{ .id = BTRFS_BLOCK_GROUP_HINT_TREE_OBJECTID,	DEFINE_NAME("block-group-hint") },
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	{ .id = BTRFS_BLOCK_GROUP_CACHE_TREE_OBJECTID,	DEFINE_NAME("block-group-cache") },
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	{ .id = BTRFS_SYNO_USAGE_TREE_OBJECTID,	DEFINE_NAME("syno-usage") },
	{ .id = BTRFS_SYNO_EXTENT_USAGE_TREE_OBJECTID,	DEFINE_NAME("syno-extent-usage") },
#endif /* MY_ABC_HERE */
#ifdef MY_ABC_HERE
	{ .id = BTRFS_SYNO_FEATURE_TREE_OBJECTID,	DEFINE_NAME("syno-feat-tree") },
#endif /* MY_ABC_HERE */
	{ .id = 0,				DEFINE_NAME("tree")	},
};

#undef DEFINE_LEVEL
#undef DEFINE_NAME

void btrfs_set_buffer_lockdep_class(u64 objectid, struct extent_buffer *eb, int level)
{
	struct btrfs_lockdep_keyset *ks;

	BUG_ON(level >= ARRAY_SIZE(ks->keys));

	/* Find the matching keyset, id 0 is the default entry */
	for (ks = btrfs_lockdep_keysets; ks->id; ks++)
		if (ks->id == objectid)
			break;

	lockdep_set_class_and_name(&eb->lock, &ks->keys[level], ks->names[level]);
}

void btrfs_maybe_reset_lockdep_class(struct btrfs_root *root, struct extent_buffer *eb)
{
	if (test_bit(BTRFS_ROOT_RESET_LOCKDEP_CLASS, &root->state))
		btrfs_set_buffer_lockdep_class(root->root_key.objectid,
					       eb, btrfs_header_level(eb));
}

#endif

/*
 * Extent buffer locking
 * =====================
 *
 * The locks use a custom scheme that allows to do more operations than are
 * available fromt current locking primitives. The building blocks are still
 * rwlock and wait queues.
 *
 * Required semantics:
 *
 * - reader/writer exclusion
 * - writer/writer exclusion
 * - reader/reader sharing
 * - spinning lock semantics
 * - blocking lock semantics
 * - try-lock semantics for readers and writers
 * - one level nesting, allowing read lock to be taken by the same thread that
 *   already has write lock
 *
 * The extent buffer locks (also called tree locks) manage access to eb data
 * related to the storage in the b-tree (keys, items, but not the individual
 * members of eb).
 * We want concurrency of many readers and safe updates. The underlying locking
 * is done by read-write spinlock and the blocking part is implemented using
 * counters and wait queues.
 *
 * spinning semantics - the low-level rwlock is held so all other threads that
 *                      want to take it are spinning on it.
 *
 * blocking semantics - the low-level rwlock is not held but the counter
 *                      denotes how many times the blocking lock was held;
 *                      sleeping is possible
 *
 * Write lock always allows only one thread to access the data.
 *
 *
 * Debugging
 * ---------
 *
 * There are additional state counters that are asserted in various contexts,
 * removed from non-debug build to reduce extent_buffer size and for
 * performance reasons.
 *
 *
 * Lock recursion
 * --------------
 *
 * A write operation on a tree might indirectly start a look up on the same
 * tree.  This can happen when btrfs_cow_block locks the tree and needs to
 * lookup free extents.
 *
 * btrfs_cow_block
 *   ..
 *   alloc_tree_block_no_bg_flush
 *     btrfs_alloc_tree_block
 *       btrfs_reserve_extent
 *         ..
 *         load_free_space_cache
 *           ..
 *           btrfs_lookup_file_extent
 *             btrfs_search_slot
 *
 *
 * Locking pattern - spinning
 * --------------------------
 *
 * The simple locking scenario, the +--+ denotes the spinning section.
 *
 * +- btrfs_tree_lock
 * | - extent_buffer::rwlock is held
 * | - no heavy operations should happen, eg. IO, memory allocations, large
 * |   structure traversals
 * +- btrfs_tree_unock
*
*
 * Locking pattern - blocking
 * --------------------------
 *
 * The blocking write uses the following scheme.  The +--+ denotes the spinning
 * section.
 *
 * +- btrfs_tree_lock
 * |
 * +- btrfs_set_lock_blocking_write
 *
 *   - allowed: IO, memory allocations, etc.
 *
 * -- btrfs_tree_unlock - note, no explicit unblocking necessary
 *
 *
 * Blocking read is similar.
 *
 * +- btrfs_tree_read_lock
 * |
 * +- btrfs_set_lock_blocking_read
 *
 *  - heavy operations allowed
 *
 * +- btrfs_tree_read_unlock_blocking
 * |
 * +- btrfs_tree_read_unlock
 *
 */

#ifdef CONFIG_BTRFS_DEBUG
static inline void btrfs_assert_spinning_writers_get(struct extent_buffer *eb)
{
	WARN_ON(eb->spinning_writers);
	eb->spinning_writers++;
}

static inline void btrfs_assert_spinning_writers_put(struct extent_buffer *eb)
{
	WARN_ON(eb->spinning_writers != 1);
	eb->spinning_writers--;
}

static inline void btrfs_assert_no_spinning_writers(struct extent_buffer *eb)
{
	WARN_ON(eb->spinning_writers);
}

static inline void btrfs_assert_spinning_readers_get(struct extent_buffer *eb)
{
	atomic_inc(&eb->spinning_readers);
}

static inline void btrfs_assert_spinning_readers_put(struct extent_buffer *eb)
{
	WARN_ON(atomic_read(&eb->spinning_readers) == 0);
	atomic_dec(&eb->spinning_readers);
}

static inline void btrfs_assert_tree_read_locks_get(struct extent_buffer *eb)
{
	atomic_inc(&eb->read_locks);
}

static inline void btrfs_assert_tree_read_locks_put(struct extent_buffer *eb)
{
	atomic_dec(&eb->read_locks);
}

static inline void btrfs_assert_tree_read_locked(struct extent_buffer *eb)
{
	BUG_ON(!atomic_read(&eb->read_locks));
}

static inline void btrfs_assert_tree_write_locks_get(struct extent_buffer *eb)
{
	eb->write_locks++;
}

static inline void btrfs_assert_tree_write_locks_put(struct extent_buffer *eb)
{
	eb->write_locks--;
}

#else
static void btrfs_assert_spinning_writers_get(struct extent_buffer *eb) { }
static void btrfs_assert_spinning_writers_put(struct extent_buffer *eb) { }
static void btrfs_assert_no_spinning_writers(struct extent_buffer *eb) { }
static void btrfs_assert_spinning_readers_put(struct extent_buffer *eb) { }
static void btrfs_assert_spinning_readers_get(struct extent_buffer *eb) { }
static void btrfs_assert_tree_read_locked(struct extent_buffer *eb) { }
static void btrfs_assert_tree_read_locks_get(struct extent_buffer *eb) { }
static void btrfs_assert_tree_read_locks_put(struct extent_buffer *eb) { }
static void btrfs_assert_tree_write_locks_get(struct extent_buffer *eb) { }
static void btrfs_assert_tree_write_locks_put(struct extent_buffer *eb) { }
#endif

/*
 * Mark already held read lock as blocking. Can be nested in write lock by the
 * same thread.
 *
 * Use when there are potentially long operations ahead so other thread waiting
 * on the lock will not actively spin but sleep instead.
 *
 * The rwlock is released and blocking reader counter is increased.
 */
void btrfs_set_lock_blocking_read(struct extent_buffer *eb)
{
	trace_btrfs_set_lock_blocking_read(eb);
	/*
	 * No lock is required.  The lock owner may change if we have a read
	 * lock, but it won't change to or away from us.  If we have the write
	 * lock, we are the owner and it'll never change.
	 */
	if (eb->lock_recursed && current->pid == eb->lock_owner)
		return;
	btrfs_assert_tree_read_locked(eb);
	atomic_inc(&eb->blocking_readers);
	btrfs_assert_spinning_readers_put(eb);
	read_unlock(&eb->lock);
}

/*
 * Mark already held write lock as blocking.
 *
 * Use when there are potentially long operations ahead so other threads
 * waiting on the lock will not actively spin but sleep instead.
 *
 * The rwlock is released and blocking writers is set.
 */
void btrfs_set_lock_blocking_write(struct extent_buffer *eb)
{
	trace_btrfs_set_lock_blocking_write(eb);
	/*
	 * No lock is required.  The lock owner may change if we have a read
	 * lock, but it won't change to or away from us.  If we have the write
	 * lock, we are the owner and it'll never change.
	 */
	if (eb->lock_recursed && current->pid == eb->lock_owner)
		return;
	if (eb->blocking_writers == 0) {
		btrfs_assert_spinning_writers_put(eb);
		btrfs_assert_tree_locked(eb);
		WRITE_ONCE(eb->blocking_writers, 1);
		write_unlock(&eb->lock);
	}
}

/*
 * Lock the extent buffer for read. Wait for any writers (spinning or blocking).
 * Can be nested in write lock by the same thread.
 *
 * Use when the locked section does only lightweight actions and busy waiting
 * would be cheaper than making other threads do the wait/wake loop.
 *
 * The rwlock is held upon exit.
 */
void __btrfs_tree_read_lock(struct extent_buffer *eb, enum btrfs_lock_nesting nest,
			    bool recurse)
{
	u64 start_ns = 0;

	if (trace_btrfs_tree_read_lock_enabled())
		start_ns = ktime_get_ns();
again:
	read_lock(&eb->lock);
	BUG_ON(eb->blocking_writers == 0 &&
	       current->pid == eb->lock_owner);
	if (eb->blocking_writers) {
		if (current->pid == eb->lock_owner) {
			/*
			 * This extent is already write-locked by our thread.
			 * We allow an additional read lock to be added because
			 * it's for the same thread. btrfs_find_all_roots()
			 * depends on this as it may be called on a partly
			 * (write-)locked tree.
			 */
			WARN_ON(!recurse);
			BUG_ON(eb->lock_recursed);
			eb->lock_recursed = true;
			read_unlock(&eb->lock);
			trace_btrfs_tree_read_lock(eb, start_ns);
			return;
		}
		read_unlock(&eb->lock);
		wait_event(eb->write_lock_wq,
			   READ_ONCE(eb->blocking_writers) == 0);
		goto again;
	}
	btrfs_assert_tree_read_locks_get(eb);
	btrfs_assert_spinning_readers_get(eb);
	trace_btrfs_tree_read_lock(eb, start_ns);
}

void btrfs_tree_read_lock(struct extent_buffer *eb)
{
	__btrfs_tree_read_lock(eb, BTRFS_NESTING_NORMAL, false);
}

/*
 * Lock extent buffer for read, optimistically expecting that there are no
 * contending blocking writers. If there are, don't wait.
 *
 * Return 1 if the rwlock has been taken, 0 otherwise
 */
int btrfs_tree_read_lock_atomic(struct extent_buffer *eb)
{
	if (READ_ONCE(eb->blocking_writers))
		return 0;

	read_lock(&eb->lock);
	/* Refetch value after lock */
	if (READ_ONCE(eb->blocking_writers)) {
		read_unlock(&eb->lock);
		return 0;
	}
	btrfs_assert_tree_read_locks_get(eb);
	btrfs_assert_spinning_readers_get(eb);
	trace_btrfs_tree_read_lock_atomic(eb);
	return 1;
}

/*
 * Try-lock for read. Don't block or wait for contending writers.
 *
 * Retrun 1 if the rwlock has been taken, 0 otherwise
 */
int btrfs_try_tree_read_lock(struct extent_buffer *eb)
{
	if (READ_ONCE(eb->blocking_writers))
		return 0;

	if (!read_trylock(&eb->lock))
		return 0;

	/* Refetch value after lock */
	if (READ_ONCE(eb->blocking_writers)) {
		read_unlock(&eb->lock);
		return 0;
	}
	btrfs_assert_tree_read_locks_get(eb);
	btrfs_assert_spinning_readers_get(eb);
	trace_btrfs_try_tree_read_lock(eb);
	return 1;
}

/*
 * Try-lock for write. May block until the lock is uncontended, but does not
 * wait until it is free.
 *
 * Retrun 1 if the rwlock has been taken, 0 otherwise
 */
int btrfs_try_tree_write_lock(struct extent_buffer *eb)
{
	if (READ_ONCE(eb->blocking_writers) || atomic_read(&eb->blocking_readers))
		return 0;

	write_lock(&eb->lock);
	/* Refetch value after lock */
	if (READ_ONCE(eb->blocking_writers) || atomic_read(&eb->blocking_readers)) {
		write_unlock(&eb->lock);
		return 0;
	}
	btrfs_assert_tree_write_locks_get(eb);
	btrfs_assert_spinning_writers_get(eb);
	eb->lock_owner = current->pid;
	trace_btrfs_try_tree_write_lock(eb);
	return 1;
}

/*
 * Release read lock. Must be used only if the lock is in spinning mode.  If
 * the read lock is nested, must pair with read lock before the write unlock.
 *
 * The rwlock is not held upon exit.
 */
void btrfs_tree_read_unlock(struct extent_buffer *eb)
{
	trace_btrfs_tree_read_unlock(eb);
	/*
	 * if we're nested, we have the write lock.  No new locking
	 * is needed as long as we are the lock owner.
	 * The write unlock will do a barrier for us, and the lock_recursed
	 * field only matters to the lock owner.
	 */
	if (eb->lock_recursed && current->pid == eb->lock_owner) {
		eb->lock_recursed = false;
		return;
	}
	btrfs_assert_tree_read_locked(eb);
	btrfs_assert_spinning_readers_put(eb);
	btrfs_assert_tree_read_locks_put(eb);
	read_unlock(&eb->lock);
}

/*
 * Release read lock, previously set to blocking by a pairing call to
 * btrfs_set_lock_blocking_read(). Can be nested in write lock by the same
 * thread.
 *
 * State of rwlock is unchanged, last reader wakes waiting threads.
 */
void btrfs_tree_read_unlock_blocking(struct extent_buffer *eb)
{
	trace_btrfs_tree_read_unlock_blocking(eb);
	/*
	 * if we're nested, we have the write lock.  No new locking
	 * is needed as long as we are the lock owner.
	 * The write unlock will do a barrier for us, and the lock_recursed
	 * field only matters to the lock owner.
	 */
	if (eb->lock_recursed && current->pid == eb->lock_owner) {
		eb->lock_recursed = false;
		return;
	}
	btrfs_assert_tree_read_locked(eb);
	WARN_ON(atomic_read(&eb->blocking_readers) == 0);
	/* atomic_dec_and_test implies a barrier */
	if (atomic_dec_and_test(&eb->blocking_readers))
		cond_wake_up_nomb(&eb->read_lock_wq);
	btrfs_assert_tree_read_locks_put(eb);
}

/*
 * Lock for write. Wait for all blocking and spinning readers and writers. This
 * starts context where reader lock could be nested by the same thread.
 *
 * The rwlock is held for write upon exit.
 */
void __btrfs_tree_lock(struct extent_buffer *eb, enum btrfs_lock_nesting nest)
	__acquires(&eb->lock)
{
	u64 start_ns = 0;

	if (trace_btrfs_tree_lock_enabled())
		start_ns = ktime_get_ns();

	WARN_ON(eb->lock_owner == current->pid);
again:
	wait_event(eb->read_lock_wq, atomic_read(&eb->blocking_readers) == 0);
	wait_event(eb->write_lock_wq, READ_ONCE(eb->blocking_writers) == 0);
	write_lock(&eb->lock);
	/* Refetch value after lock */
	if (atomic_read(&eb->blocking_readers) ||
	    READ_ONCE(eb->blocking_writers)) {
		write_unlock(&eb->lock);
		goto again;
	}
	btrfs_assert_spinning_writers_get(eb);
	btrfs_assert_tree_write_locks_get(eb);
	eb->lock_owner = current->pid;
	trace_btrfs_tree_lock(eb, start_ns);
}

void btrfs_tree_lock(struct extent_buffer *eb)
{
	__btrfs_tree_lock(eb, BTRFS_NESTING_NORMAL);
}

/*
 * Release the write lock, either blocking or spinning (ie. there's no need
 * for an explicit blocking unlock, like btrfs_tree_read_unlock_blocking).
 * This also ends the context for nesting, the read lock must have been
 * released already.
 *
 * Tasks blocked and waiting are woken, rwlock is not held upon exit.
 */
void btrfs_tree_unlock(struct extent_buffer *eb)
{
	/*
	 * This is read both locked and unlocked but always by the same thread
	 * that already owns the lock so we don't need to use READ_ONCE
	 */
	int blockers = eb->blocking_writers;

	BUG_ON(blockers > 1);

	btrfs_assert_tree_locked(eb);
	trace_btrfs_tree_unlock(eb);
	eb->lock_owner = 0;
	btrfs_assert_tree_write_locks_put(eb);

	if (blockers) {
		btrfs_assert_no_spinning_writers(eb);
		/* Unlocked write */
		WRITE_ONCE(eb->blocking_writers, 0);
		/*
		 * We need to order modifying blocking_writers above with
		 * actually waking up the sleepers to ensure they see the
		 * updated value of blocking_writers
		 */
		cond_wake_up(&eb->write_lock_wq);
	} else {
		btrfs_assert_spinning_writers_put(eb);
		write_unlock(&eb->lock);
	}
}

/*
 * Set all locked nodes in the path to blocking locks.  This should be done
 * before scheduling
 */
void btrfs_set_path_blocking(struct btrfs_path *p)
{
	int i;

	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		if (!p->nodes[i] || !p->locks[i])
			continue;
		/*
		 * If we currently have a spinning reader or writer lock this
		 * will bump the count of blocking holders and drop the
		 * spinlock.
		 */
		if (p->locks[i] == BTRFS_READ_LOCK) {
			btrfs_set_lock_blocking_read(p->nodes[i]);
			p->locks[i] = BTRFS_READ_LOCK_BLOCKING;
		} else if (p->locks[i] == BTRFS_WRITE_LOCK) {
			btrfs_set_lock_blocking_write(p->nodes[i]);
			p->locks[i] = BTRFS_WRITE_LOCK_BLOCKING;
		}
	}
}

/*
 * This releases any locks held in the path starting at level and going all the
 * way up to the root.
 *
 * btrfs_search_slot will keep the lock held on higher nodes in a few corner
 * cases, such as COW of the block at slot zero in the node.  This ignores
 * those rules, and it should only be called when there are no more updates to
 * be done higher up in the tree.
 */
void btrfs_unlock_up_safe(struct btrfs_path *path, int level)
{
	int i;

	if (path->keep_locks)
		return;

	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		if (!path->nodes[i])
			continue;
		if (!path->locks[i])
			continue;
		btrfs_tree_unlock_rw(path->nodes[i], path->locks[i]);
		path->locks[i] = 0;
	}
}

/*
 * Loop around taking references on and locking the root node of the tree until
 * we end up with a lock on the root node.
 *
 * Return: root extent buffer with write lock held
 */
struct extent_buffer *btrfs_lock_root_node(struct btrfs_root *root)
{
	struct extent_buffer *eb;

	while (1) {
		eb = btrfs_root_node(root);

		btrfs_maybe_reset_lockdep_class(root, eb);
		btrfs_tree_lock(eb);
		if (eb == root->node)
			break;
		btrfs_tree_unlock(eb);
		free_extent_buffer(eb);
	}
	return eb;
}

/*
 * Loop around taking references on and locking the root node of the tree until
 * we end up with a lock on the root node.
 *
 * Return: root extent buffer with read lock held
 */
struct extent_buffer *__btrfs_read_lock_root_node(struct btrfs_root *root,
						  bool recurse)
{
	struct extent_buffer *eb;

	while (1) {
		eb = btrfs_root_node(root);

		btrfs_maybe_reset_lockdep_class(root, eb);
		__btrfs_tree_read_lock(eb, BTRFS_NESTING_NORMAL, recurse);
		if (eb == root->node)
			break;
		btrfs_tree_read_unlock(eb);
		free_extent_buffer(eb);
	}
	return eb;
}

/*
 * DREW locks
 * ==========
 *
 * DREW stands for double-reader-writer-exclusion lock. It's used in situation
 * where you want to provide A-B exclusion but not AA or BB.
 *
 * Currently implementation gives more priority to reader. If a reader and a
 * writer both race to acquire their respective sides of the lock the writer
 * would yield its lock as soon as it detects a concurrent reader. Additionally
 * if there are pending readers no new writers would be allowed to come in and
 * acquire the lock.
 */

void btrfs_drew_lock_init(struct btrfs_drew_lock *lock)
{
	atomic_set(&lock->readers, 0);
	atomic_set(&lock->writers, 0);
	init_waitqueue_head(&lock->pending_readers);
	init_waitqueue_head(&lock->pending_writers);
}

/* Return true if acquisition is successful, false otherwise */
bool btrfs_drew_try_write_lock(struct btrfs_drew_lock *lock)
{
	if (atomic_read(&lock->readers))
		return false;

	atomic_inc(&lock->writers);

	/* Ensure writers count is updated before we check for pending readers */
	smp_mb__after_atomic();
	if (atomic_read(&lock->readers)) {
		btrfs_drew_write_unlock(lock);
		return false;
	}

	return true;
}

void btrfs_drew_write_lock(struct btrfs_drew_lock *lock)
{
	while (true) {
		if (btrfs_drew_try_write_lock(lock))
			return;
		wait_event(lock->pending_writers, !atomic_read(&lock->readers));
	}
}

void btrfs_drew_write_unlock(struct btrfs_drew_lock *lock)
{
	atomic_dec(&lock->writers);
	cond_wake_up(&lock->pending_readers);
}

void btrfs_drew_read_lock(struct btrfs_drew_lock *lock)
{
	atomic_inc(&lock->readers);

	/*
	 * Ensure the pending reader count is perceieved BEFORE this reader
	 * goes to sleep in case of active writers. This guarantees new writers
	 * won't be allowed and that the current reader will be woken up when
	 * the last active writer finishes its jobs.
	 */
	smp_mb__after_atomic();

	wait_event(lock->pending_readers, atomic_read(&lock->writers) == 0);
}

void btrfs_drew_read_unlock(struct btrfs_drew_lock *lock)
{
	/*
	 * atomic_dec_and_test implies a full barrier, so woken up writers
	 * are guaranteed to see the decrement
	 */
	if (atomic_dec_and_test(&lock->readers))
		wake_up(&lock->pending_writers);
}
