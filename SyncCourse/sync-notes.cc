// Outline:

// Some sample code for explaining how to mix synchronization and
// reference counting.

// Simple buffer package with buffers, inodes, hash tables and LRU.
// Buffers keep strong reference to inodes.  Users get strong
// references to buffers and inodes.  Hash tables entries keep weak
// references to buffers.  LRU entries keep weak references to
// buffers.  Inode pointers are located via a separate lookup system.
//
// We want to have a lock per hash bucket for performance.  Initially
// one LRU, but perhaps for performance we look a multiple at some
// point.
//
// Lock hash bucket first, then lruLock, then individual buffer locks.
//
// We want to be able to find a buffer with only the specific hashLock
// and buffer lock.  Same for marking buffer as dirty, or verifying
// that it is already marked as dirty.  Recycling can use LRU lock,
// and LRU doesn't have to be updated on every reference.  It should
//
// To do a lookup:

Buffer *BufSys::lookup(Inode *ip, uint64_t off) {
    {
        hashIx = computeHash(ip, off);
        _hashLocks[hashIx].lock();
        buffer = findInBucket(hashIx, ip, off);
        if (buffer != nullptr) {
            return buffer;
        }

        // Should recycle a buffer now but perhaps a smarter choice is
        // allocate a new buffer and do the reclaim on the way out.
        buffer = new Buffer(ip, offset);
        hashIn(buffer->hashEntry);
        addToLru(buffer->lruEntry); // grabs lru lock
    }

    // Now recycle entries.  May exceed quota by max # of threads
    // executing.
    recycleAsNeeded();
}

// TODO: get rid of LRU lock safely
void Buffer:markDirty(Buffer *buffer) {
    _lruLock.lock();
    _bufferLock.lock()
    _dirty = true;
    _dirtyRef = this;

    if (_inLru) {
        removeFromLru(this);
        _inLru = false;
    }
    _bufferLock.unlock();
    _lruLock.unlock();
}

// To do an invalidation:

// RECYCLE: we want to remove the entry from the hash table and LRU,
// but we also need to ensure that the buffer hasn't been dirtied.  We
// need to ensure that we don't have multiple copies of a buffer, to
// avoid a dirty buffer and a stale clean buffer from coexisting (or
// worse, two different dirty buffers).  We have several choices:
//
// 1 -- LRU and _dirtyRef are strong, hash is weak pointer.  If
// dirtyRef is set, buffer isn't in LRU, unless buffer is invalidated.
// This invariant is protected by lruLock + buffer lock.  So, to
// recycle a buffer, grab lruLock and buffer lock.  Verify dirtyRef is
// nullptr.  If so, hash out buffer, remove from LRU.  Mark buffer as
// invalid (under bufferLock).  Strong references at this point only
// come from user.  Eventually all strong references vanish, and
// buffer is freed.  Custom deleter could manage pool of VM.
//
// Note one issue is that the buffer can't be actually recycled until
// all users references are released.  This might be important if the
// buffers own kernel memory and we have to maintain a *strict*
// maximum number of buffers.
//

void
BufSys:recycleAsNeeded() {
    // XXX This is wrong.
    while (true) {
        shared_ptr<Buffer> buffer;
        _lruLock.lock();
        head = _lru.pop();    // bumps ref count while holding
        buffer = head->_buffer;
        buffer->_lock.lock();
        // can't mark buffer as dirty and set dirtyRef without holding lruLock,
        // so this should always be true.
        osp_assert(buffer->_dirtyRef == nullptr);
        buffer->_lock.unlock();
        _lruLock.release();

        head->_buffer = nullptr;
        head = nullptr;

        // Buffer can't change identity with this design.  So, hashOut
        // will grab the relevant hashLock and drop the weak reference
        // to buffer.
        hashOut(buffer);
    }
}
