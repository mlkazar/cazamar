#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#import "MFANAqStreamBuffer.h"
#import "MFANCGUtil.h"

#include "osp.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>

#include <string>

// Some useful constants
static int _bufferStaticSetup = 0;
static pthread_mutex_t _bufferMutex;
static const uint32_t _kBytesPerBlock = 16*1024;
static const uint32_t _kTrailerBytes = 2;
static const uint16_t _kMagic = 0x0301;			// for records
static const uint16_t _kMagicMask = 0x0FFF;		// mask
static const uint16_t _kMagicFlagsShift = 12;
static const uint16_t _kMagicFlagsMask = 0xF;
static const uint16_t _kMagicFlagError = 1;

// if we find a bug that might have left corrupt files in place,
// change this magic # to ensure the files' contents get reset.
static const uint16_t _kFileMagic = 0x0304;		// for the whole file make 0304 on nextrel

static const uint16_t _kTrailerMagic = 0x0924;
static const uint32_t _kMaxValidBlocks = 32;
static const uint32_t _kMaxDiskPct = 50;	// maximum unused disk space before reclaim DEBUG

// ---------------------------------------------------------------------------
// MFANAqStreamPacket
// ---------------------------------------------------------------------------

@implementation MFANAqStreamPacket {
    uint64_t _startMs;
    uint32_t _durationMs;
    uint16_t _flags;

    std::string _data;
    bool _read;
    NSString *_playingSong;
    AudioStreamPacketDescription _descr;
}

uint16_t setMagicFlags(uint16_t magicFlags) {
    return _kMagic | (magicFlags << _kMagicFlagsShift);
}

uint16_t getMagicFlags(uint16_t recordMagic) {
    return (recordMagic >> _kMagicFlagsShift) & _kMagicFlagsMask;
}

- (void) setErrorCode: (int32_t) code {
    if (code != 0)
	_flags |= _kMagicFlagError;
    else
	_flags &= ~_kMagicFlagError;
}

+ (uint16_t) kMagicFlagError {
    return 0x1;
}

- (MFANAqStreamPacket *) init {
    self = [super init];
    if (self != nil) {
        _read = NO;
	_flags = 0;
    }
    return self;
}

- (AudioStreamPacketDescription *) getDescrAddr {
    return &_descr;
}

- (int32_t) addData: (char *) data descr: (AudioStreamPacketDescription *) descr {
    _data.append(data, descr->mDataByteSize);
    _descr = *descr;
    return 0;
}

- (char *) getData {
    return _data.data();
}

- (void) setData: (std::string ) inData {
    _data = inData;
}

// *not* null terminated
- (char *) getDataBytes {
    return (char *) _data.data();
}

- (uint32_t) getLength {
    return (uint32_t) _data.length();
}

- (void) getDescr: (AudioStreamPacketDescription *) descr {
    *descr = _descr;
}

@end

class MFANAqStreamBlockHolder {
 public:
    char *_blockDatap;

    MFANAqStreamBlockHolder();

    ~MFANAqStreamBlockHolder() {
	if (_blockDatap) {
	    free(_blockDatap);
	    _blockDatap = nullptr;
	}
    }

    char *data() {
	return _blockDatap;
    }
};

@implementation MFANAqStreamFile {
    NSMutableArray<MFANAqStreamBlock *> *_blocks;
    NSMutableOrderedSet<MFANAqStreamBlock *> *_lru;
    uint32_t _fileId;
    uint32_t _gcBlockShift;
    int _readFd;
    int _writeFd;
};

- (MFANAqStreamFile *) init {
    self = [super init];
    if (self != nil) {
	_fileId = ~0U;
	_gcBlockShift = 0;
	_readFd = -1;
	_writeFd = -1;
	_blocks = [[NSMutableArray alloc] init];
	_lru = [[NSMutableOrderedSet alloc] init];
    }
    return self;
}

- (void) dealloc {
    if (_readFd >= 0) {
	close(_readFd);
	_readFd = -1;
    }
    if (_writeFd >= 0) {
	close(_writeFd);
	_writeFd = -1;
    }
}

@end

// ---------------------------------------------------------------------------
// MFANAqStreamReader
//
// This is really part of MFANAqStreamBuffer in the sense that it
// grabs locks and examines its internal state.
//
// It caches a _blockIx and _packetIx field that represent indices
// into the streamFile->_blocks array and packet->packetArray
// respectively.  These indices point to the next packet to be read by
// the streamReader.  A packetIx equal to the count in any block means
// the beginning of the next block.  This means no data is present if
// the blockIx is the last block in the streamBuffer.
//
// Note that one invariant is that there is always one block in the
// buffer: the buffer is created with one, and the last block is never
// deleted.
// ---------------------------------------------------------------------------

@implementation MFANAqStreamReader {
    MFANAqStreamBuffer *_streamBuffer;
    uint64_t _recordMs;             // current position in the stream (ms)
    uint32_t _blockIx;
    uint32_t _packetIx;                   // index of the next packet to read
    bool _closed;
    MFANAqStreamBlock *_pinned;		// pinned block
    bool _noWait;
}

- (void) dealloc {
    // special cleanup required for pinned blocks
    if (_pinned != nil) {
	[_streamBuffer unpin: _pinned];
	_pinned = nil;
    }
}

- (MFANAqStreamReader *) initWithBuffer: (MFANAqStreamBuffer *) buffer {
    self = [super init];
    if (self != nil) {
        _streamBuffer = buffer;
	pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);
        MFANAqStreamPacket *packet;

	MFANAqStreamBlock *block = [buffer lastBlockSetIndex:&_blockIx];
	_pinned = [buffer pin: _pinned];

        uint32_t packetCount;

	// Last block is always valid.  Start past the last record
	// already in the stream
	packetCount = (uint32_t) [block.packetArray count];
	packet = [buffer.packetArray lastObject];
	if (packet != nil) {
	    _recordMs = packet.startMs + packet.durationMs;
	    _packetIx = packetCount;
	} else {
	    _recordMs = block.baseMs;
	    _packetIx = 0;
	}

	_closed = false;
	_noWait = false;

	pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
    }

    return self;
}

// Called with bufferMutex held.  Returns true if the recordMs field
// is present in the packet pointed to by the blockIx / packetIx
// parameters.
- (bool) indicesAreValid {
    return ([_streamBuffer blockIx: _blockIx packetIx: _packetIx containsMs: _recordMs]);
}

- (void) updatePinnedBlock: (MFANAqStreamBlock *) block {
    if (block != _pinned) {
	if (_pinned != nil)
	    [_streamBuffer unpin: _pinned];
	_pinned = [_streamBuffer pin: block];
    }
}

- (void) seek: (uint64_t) ms whence: (int) how {
    MFANAqStreamBlock *block;
    uint32_t packetCount;

    pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);
    block = [_streamBuffer findBlockAtMs: ms setIndex:&_blockIx];
    _packetIx = [block findPacketIx: ms];
    packetCount = (uint32_t) [block.packetArray count];

    // seek to last packet available near our seek point.
    if (packetCount == 0) {
	_recordMs = block.baseMs;
    } else if (_packetIx >= packetCount)
	_recordMs = block.packetArray[packetCount-1].startMs;
    else
	_recordMs = block.packetArray[_packetIx].startMs;

    NSLog(@"==>seek to ms=%lld bix=%d pix=%d", ms, _blockIx, _packetIx);
    pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);

    [self updatePinnedBlock: block];

    // wake up any pending read
    pthread_cond_broadcast([_streamBuffer packetArrayCv]);
}

- (uint64_t) tell {
    uint64_t rval;
    pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);
    rval = _recordMs;
    pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
    return rval;
}

// Check whether at least targetBytes of audio is queued ahead of the current
// read position.  Blocks until there is, or until the buffer is torn down /
// the streamer finishes.  Returns YES if the target was met.
- (bool) waitForAtLeast: (uint64_t) targetBytes {
    uint32_t blockIx;
    uint32_t packetIx;
    uint32_t blockCount;
    uint32_t startIx;
    uint32_t packetCount;
    MFANAqStreamBlock *block;
    uint64_t totalBytes;
    MFANAqStreamPacket *packet;

    pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);

    NSLog(@"waitforatleast starts");
    bool firstBlock;
    while(true) {
        if ((_streamBuffer.shuttingDown && !_noWait) || _closed) {
	    // failed, so return false
            break;
        }

	blockCount = (uint32_t) [_streamBuffer.streamFile.blocks count];

        if (!self.indicesAreValid) {
	    block = [_streamBuffer findBlockAtMs: _recordMs setIndex:&blockIx];
	    packetIx = [block findPacketIx: _recordMs];
        } else {
	    blockIx = _blockIx;
	    packetIx = _packetIx;
	    if (blockIx >= blockCount) {
		pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
		return false;
	    }
	    block = _streamBuffer.streamFile.blocks[blockIx];
	}

	packetCount = (uint32_t) [block.packetArray count];
	firstBlock = true;
	totalBytes = 0;
	while(true) {
	    if (firstBlock) {
		startIx = packetIx;
	    } else {
		startIx = 0;
	    }

	    if (firstBlock) {
		// if the first block isn't valid, we'll see a zero
		// packetCount, and we won't count the contents of
		// this block as available data.  That shouldn't
		// happen often and shouldn't matter much when it does
		// occur.
		for(uint32_t i=startIx; i<packetCount; i++) {
		    packet = block.packetArray[i];
		    totalBytes += [packet getLength];
		    if (totalBytes >= targetBytes) {
			pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
			NSLog(@"waitforatleast ends");
			return true;
		    }
		}
	    } else {
		totalBytes += block.diskBytesUsed;
		if (totalBytes >= targetBytes) {
		    pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
		    NSLog(@"waitforatleast ends");
		    return true;
		}
	    }

	    blockIx++;
	    if (blockIx >= blockCount) {
		break;
	    }
	    block = _streamBuffer.streamFile.blocks[blockIx];
	    firstBlock = false;
	}

	// if we make it here without returning, we have to wait for more data to
	// get added.
        pthread_cond_wait([_streamBuffer packetArrayCv], [MFANAqStreamBuffer bufferMutex]);
        continue;
    }

    // if we get here, we ran into a problem and should return false.
    pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
    NSLog(@"waitforatleast ends with failure");
    return false;
}

- (bool) hasData {
    bool rval;
    MFANAqStreamBlock *block;
    uint32_t blockIx;
    uint32_t packetIx;
    uint32_t blockCount;

    // remember that blockIx, packetIx points to the next packet to
    // return.
    pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);

    NSLog(@"hasData starts");
    blockCount = (uint32_t) [_streamBuffer.streamFile.blocks count];

    if (!self.indicesAreValid) {
	block = [_streamBuffer findBlockAtMs: _recordMs setIndex:&blockIx];
	packetIx = [block findPacketIx: _recordMs];
    } else {
	blockIx = _blockIx;
	packetIx = _packetIx;
	if (blockIx >= blockCount)
	    block = nil;
	else
	    block = _streamBuffer.streamFile.blocks[blockIx];
    }

    if (blockIx < blockCount - 1) {
	// there's at least a whole unprocessed block after blockIx
	rval = true;
    } if (blockIx >= blockCount) {
	// no block at all, so no data
	rval = false;
    } else {
	// blockIx points to the last block more packets in this
	// current block.  If packetIx points at a record in this
	// block, return true
	if (packetIx < [block.packetArray count])
	    rval = true;
	else
	    rval = false;
    }

    pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);

    return rval;
}

- (void) close {
    pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);
    _closed = YES;
    pthread_cond_broadcast([_streamBuffer packetArrayCv]);
    pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
}

- (MFANAqStreamPacket *) read {
    MFANAqStreamPacket *packet = nil;
    MFANAqStreamBlock *block = nil;
    uint32_t packetCount;
    uint32_t blockCount;
    uint32_t lastBlockIx;
    uint32_t loops = 0;

    pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);
    NSLog(@"read starts for recordMs=%lld", _recordMs);
    while(true) {
        // Abort if the buffer itself is being shutdown or the reader
        // was closed.
        if ((_streamBuffer.shuttingDown && !_noWait) || _closed) {
            pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
	    NSLog(@"read done --> closed");
            return nil;
        }

        if (!self.indicesAreValid) {
	    block = [_streamBuffer findBlockAtMs: _recordMs setIndex:&_blockIx];
	    _packetIx = [block findPacketIx: _recordMs];
	    NSLog(@"read index !valid, using bix=%d pix=%d baseMs=%lld o=%llx (recordMs=%lld)",
		  _blockIx, _packetIx, block.baseMs, block.fileOffset, _recordMs);
        } else {
	    block = _streamBuffer.streamFile.blocks[_blockIx];
	    NSLog(@"read index valid using bix=%d baseMs=%lld for recordMs=%lld",
		  _blockIx, block.baseMs, _recordMs);
	}

	if (![block validContents]) {
	    [_streamBuffer fillBlock: block];
	    NSLog(@"read filling block ms=%lld", block.baseMs);
	    continue;
	}

	// prevent block we're reading from being recycled
	[self updatePinnedBlock: block];

	// get count after making sure block is valid
	packetCount = (uint32_t) [block.packetArray count];
	blockCount = (uint32_t) [_streamBuffer.streamFile.blocks count];

	// blockCount is never 0; it is one upon creation and we never
	// get rid of the block at the end collecting packets.
	lastBlockIx = blockCount - 1;

        if ( _blockIx > lastBlockIx ||
	     (_blockIx == lastBlockIx &&
	      _packetIx >= packetCount)) {

            // No packet at the current index.  Handle nowait case first.
	    if (_noWait) {
		pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
		return nil;
	    }

	    NSLog(@"read waiting for more data bix=%d pix=%d", _blockIx, _packetIx);
            pthread_cond_wait([_streamBuffer packetArrayCv], [MFANAqStreamBuffer bufferMutex]);
            continue;
        }

	// it's possible that we've read all the packets from a block
	// and just need to get to the next block.
	if (_packetIx >= packetCount) {
	    // if we're here, _blockIx must not have been the last
	    // block's index, or we'd have gone to sleep and looped
	    // around above.
	    _blockIx++;
	    _packetIx = 0;
	    if (++loops > 10) {
		osp_assert("foo" == nullptr);
	    }
	    continue;
	}

	// otherwise we can actually return a packet
        packet = block.packetArray[_packetIx];
        _recordMs = packet.startMs + packet.durationMs;
        _packetIx++;
        break;
    }
    packet.read = YES;
    pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
    NSLog(@"returned packet for recordMs=%lld w/start=%lld block start=%lld:%lld",
	  _recordMs, packet.startMs, block.baseMs, block.durationMs);
    return packet;
}

@end

// Design for pruning the stremFile:
//
// Create a second file.  This file will contain the new blocks.
// Decide how many blocks will be removed, and put that value in the
// streamFile.
//
// A flag in the streamFile, protected by the buffer mutex, indicates
// that a GC is in progress, and new blocks should be allocated from
// the secondary file as they're allocated by addPacket.
//
// At this time, we kick off a migration thread that iterates over all
// the blocks, setting the ioRunning flag for the block, and then
// copying the block to the secondary file, and then clearing the
// ioRunning flag.  Actually, this has been disabled for now since we
// don't need to do the main IO without locks (the lock contention is
// rare enough that it isn't worth the trouble of managing ioRunning
// flags, and allowing a GC to run while we're adding blocks to the
// file.
//
// Once all the blocks in the file has been migrated, we grab the
// mutex lock, rename the secondary file to be primary, and clear the
// per-block bit indicating the block resides in the secondary file.

@implementation MFANAqStreamBlock {
    uint64_t _baseMs;		// time stamp of first record
    NSMutableArray<MFANAqStreamPacket *> *_packetArray;
    uint64_t _durationMs;
    uint64_t _fileOffset;	// offset in file where data is located.
    uint32_t _diskBytesUsed;	// disk space used
    BOOL _valid;		// packet data is present in memory and sealed
    BOOL _dirty;		// packet data needs to be written to file
    BOOL _sealed;		// contents won't change again
    BOOL _ioRunning;		// either filling or cleaning
    BOOL _inLru;		// are we in the LRU queue
    uint8_t _pinCount;		// how many pins there are
}

- (MFANAqStreamBlock *) initWithBuffer: (MFANAqStreamBuffer *) buffer {
    self = [super init];
    if (self != nil) {
	_valid = false;
	_dirty = false;
	_sealed = false;
	_ioRunning = false;
	_fileOffset = _kBytesPerBlock;	// init as if restore isn't done, so skip header
	_diskBytesUsed = 0;
	_durationMs = 0;
	_inLru = false;
	_pinCount = 0;
	_packetArray = [[NSMutableArray alloc] init];

	// gets marked dirty and valid when it gets sealed
	[buffer.streamFile.blocks addObject: self];
    }

    return self;
}

- (BOOL) validContents {
    // valid is never set on the unsealed block at the end.
    return _valid || !_sealed;
}

- (uint32_t) findPacketIx: (uint64_t) ms {
    uint32_t ix;
    MFANAqStreamPacket *packet;

    // caller should have done this for us
    osp_assert([self validContents]);

    for(ix = 0; ix < [_packetArray count]; ix++) {
        packet = _packetArray[ix];
        if (packet.startMs >= ms)
            break;
    }

    NSLog(@"findPacketIx for %lld ms off=%llx returns pix=%u/%u ms=%lld",
	  ms, _fileOffset, ix, (uint32_t) [_packetArray count],
	  packet.startMs);
    return ix;
}
@end

// ---------------------------------------------------------------------------
// MFANAqStreamBuffer
// ---------------------------------------------------------------------------

MFANAqStreamBlockHolder::MFANAqStreamBlockHolder() {
    _blockDatap = (char *) malloc(_kBytesPerBlock);
}

@implementation MFANAqStreamBuffer {
    pthread_cond_t _packetArrayCv;	// data arrival CV
    pthread_cond_t _pthreadReqCv;	// pthread should look for work CV
    pthread_cond_t _pthreadDoneCv;	// waiting for pthread exit.
    pthread_cond_t _blockIoCv;		// block fill / clean wait/completed
    MFANAqStreamFile *_streamFile;
    uint64_t _lastPacketEndMs;
    uint64_t _firstPacketStartMs;
    uint64_t _fileSize;

    // lock for protecting against file blocks being removed from disk
    // file or from blocks being removed from _streamFile.blocks
    // array.  Code should be robust against adding packets to
    // existing last block, or adding new empty blocks at the end.
    pthread_mutex_t _blockMutex;

    // The valid blocks may be anywhere in the file's _blocks array.
    // The dirty blocks are supposed to be all at the end of the
    // array, although some race conditions might violate that.
    //
    // Note that these counters don't include the current open block at the
    // end of the file.
    uint32_t _validBlocks;		// number of blocks with valid packet contents
    uint32_t _dirtyBlocks;		// number of blocks not written to backing file

    NSThread *_streamBufferThread;
    BOOL _pthreadDoWork;
    BOOL _pthreadDone;

    NSThread *_gcBufferThread;
    BOOL _gcRunning;
    int _gcOldFd;
    int _gcNewFd;

    // We keep a copy of the dataFormat (for the whole file) and the
    // adtsHeader (for AAC files) in a header, to use if the network
    // stream isn't available.  These fields are initalized from the
    // file's header block, and updated (along with the header block)
    // if a network stream starts up.
    AudioStreamBasicDescription _dataFormat;

    // we save a prototype of the stream's ADTS header, with the
    // length zeroed and the 'CRC not present' flag set, to use if the
    // network stream isn't available.
    char _adtsHeader[7];
}

+ (pthread_mutex_t *) bufferMutex {
    return &_bufferMutex;
}

// The first block has a separate header.  It starts off with 2 bytes
// of _kFileMagic, and is followed by the AudioStreamBasicDescription
// for the stream.
//
// The remaining blocks are just a set of records, described below,
// ending with an end record.
//
// The format of a packet on disk consists of:
//
// 2 bytes magic # (0xaa for regular data, 0xee for end record)
//
// 2 bytes length of packet data
//
// <n> bytes of packet data
//
// 4 bytes of packet duration in ms
//
// 2 bytes of descriptor length
//
// <n> bytes of descriptor in binary
//
// 2 bytes of song name length
//
// <n> bytes of song name
//
- (uint32_t) packetDiskSize: (MFANAqStreamPacket *) packet {
    return (uint32_t) (12 + [packet getLength] + [packet.playingSong length] +
		       sizeof(AudioStreamPacketDescription));
}

- (BOOL) blockIx: (uint32_t) blockIx packetIx: (uint32_t) packetIx containsMs: (uint64_t) ms {
    MFANAqStreamBlock *block;
    MFANAqStreamBlock *nextBlock;
    uint32_t packetCount;
    MFANAqStreamPacket *packet;
    MFANAqStreamPacket *nextPacket;

    // index isn't valid if it points beyond end of array
    if (blockIx >= [_streamFile.blocks count])
	return false;

    // otherwise this is the block to check
    block = _streamFile.blocks[blockIx];

    if (blockIx >= [_streamFile.blocks count] - 1)
	nextBlock = nil;
    else
	nextBlock = _streamFile.blocks[blockIx+1];

    if (ms >= block.baseMs &&
	(nextBlock == nil || ms < nextBlock.baseMs)) {
	// in range of this block, which may have no packets, but only
	// at the time before any packets have been added
	packetCount = (uint32_t) [block.packetArray count];
	if (packetCount == 0) {
	    if (packetIx == 0)
		return true;
	} else if (packetIx == packetCount) {
	    packet = block.packetArray[packetCount-1];
	    // packetIx can't be 0 in this branch.
	    if ( ms >= packet.startMs &&
		 ms < packet.startMs + packet.durationMs)
		return true;
	} else if (packetIx < packetCount) {
	    packet = block.packetArray[packetIx];
	    if (packetIx >= [block.packetArray count]-1)
		nextPacket = nil;
	    else 
		nextPacket = block.packetArray[packetIx+1];
	    if ( ms >= packet.startMs &&
		 (nextPacket == nil || ms < nextPacket.startMs))
		return true;
	}
    }

    return false;
}

- (NSString *) nameAt: (uint64_t) ms {
    MFANAqStreamBlock *block;
    uint32_t blockIx;
    uint32_t packetIx;
    MFANAqStreamPacket *packet;
    NSString *rval;
    uint32_t packetCount;

    pthread_mutex_lock(&_bufferMutex);
    block = [self findBlockAtMs: ms setIndex:&blockIx];
    packetIx = [block findPacketIx: ms];
    packetCount = (uint32_t) [block.packetArray count];

    if (packetCount == 0) {
	return @"";
    } else if (packetIx >= packetCount)
	packet = block.packetArray[packetCount-1];
    else {
	packet = block.packetArray[packetIx];
    }

    rval = packet.playingSong;

    pthread_mutex_unlock(&_bufferMutex);

    return rval;
}

// these functions not only return a block, but ensure that its
// contents are valid.  The last block is special; it isn't marked as
// valid, but it is always valid and never in the LRU queue.
- (MFANAqStreamBlock *) lastBlockSetIndex: (uint32_t *) indexp {
    // last block is always valid
    uint32_t blockIndex = (uint32_t) [_streamFile.blocks count] - 1;
    if (indexp != nullptr)
	*indexp = blockIndex;
    return _streamFile.blocks[blockIndex];
}

// Called with the buffer lock held, returns the block (and sets
// its index) and valid.
- (MFANAqStreamBlock *) findBlockAtMs: (uint64_t) ms
			     setIndex: (uint32_t *) indexp {
    uint32_t i;
    MFANAqStreamBlock *block;
    uint32_t blockCount;
    uint32_t ix;

    // remember we need to validate the block is still valid
    // after reading it.
    while(true) {
	blockCount = (uint32_t) [_streamFile.blocks count];
	for(i=0;i<blockCount;i++) {
	    block = _streamFile.blocks[i];
	    if ( ms < block.baseMs + block.durationMs)
		break;
	}
	// block is set to last one if none match condition, which is
	// what we want.  Note that the last block can't be reclaimed,
	// and isn't marked as valid, but it has valid contents.
	ix = (i<blockCount? i : i-1);
	if ([block validContents]) {
	    *indexp = ix;
	    return block;
	}

	// fill block, but since may have dropped lock, need to
	// revalidate.
	NSLog(@"findBlockAtMs baseMs=%lld needs to fill block at index %d (%d)",
	      block.baseMs, i, blockCount);
	[self fillBlock: block];
    }
}

- (void) unpin: (MFANAqStreamBlock *) block {
    if (block==nil)
	return;

    osp_assert(block.pinCount > 0);
    block.pinCount--; 
   [self fixLru: block];
}

- (MFANAqStreamBlock *) pin: (MFANAqStreamBlock *) block {
    if (block == nil)
	return nil;
    block.pinCount++;
    [self fixLru: block];
    return block;
}

// Call after doing initFromFileId
- (int32_t) restoreBlocksFromFile {
    struct stat tstat;
    int32_t code;
    uint32_t blockCount;
    uint32_t ix;
    MFANAqStreamBlock *block;
    uint32_t durationMs;

    pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);

    // This may truncate the file if the header looks bad.
    [self readHeaderBlock];

    code = fstat(_streamFile.readFd, &tstat);
    if (code < 0) {
	pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
	return -1;
    }

    // note that blockCount *include* the header block.
    blockCount = (uint32_t) (tstat.st_size / _kBytesPerBlock);
    uint64_t baseMs = 0;

    NSLog(@"=p=streambuf starting check of %d blocks", blockCount);

    // This isn't safe to do during normal operation, but during
    // initialization there's nothing else going on, so we don't have
    // to worry about other references to blocks that are dirty or
    // valid.
    [_streamFile.blocks removeAllObjects];
    osp_assert(_dirtyBlocks == 0 && _validBlocks == 0);
    _firstPacketStartMs = 0;
    for(ix=1; ix<blockCount; ix++) {
	block = [[MFANAqStreamBlock alloc] initWithBuffer: self];
	block.ioRunning = true;
	block.dirty = false;
	block.fileOffset = ix * _kBytesPerBlock;
	block.inLru = false;
	block.baseMs = baseMs;
	block.sealed = true;
	block.valid = false;
	block.diskBytesUsed = _kBytesPerBlock;
	code = [self readPacketsFromBlock: block duration:&durationMs];

	// make it look valid and sealed now that it is present
	block.ioRunning = false;
	[block.packetArray removeAllObjects];
	[self fixLru: block];

	// and manage the duration and ms timestamps
	block.durationMs = durationMs;
	block.baseMs = baseMs;
	baseMs += durationMs;

	// if read failed, stop
	if (code != 0) {
	    [_streamFile.blocks removeLastObject];
	    pthread_cond_broadcast(&_blockIoCv);
	    break;
	}
    }

    if (blockCount >= 1 && ix == blockCount) {
	NSLog(@"=p=streambuf restored all %d blocks successfully for fileId=%d",
	      blockCount, _streamFile.fileId);
	// If we had a block count of 2, for example, ix == 2, and we
	// have a header block and the block with ix == 1.  The next
	// block to allocate should have a file offset of
	// 2*_kBytesPerBlock.
    } else {
	// if failed on ix == 2, for example, ix == 1 is good, and we
	// have the header block, too, so we want to truncate to two
	// blocks.  We should in this case have one block in the
	// blocks array.  Also in this case, the next block to use is
	// ix=2, at file offset 2*_kBytesPerBlock
	ftruncate(_streamFile.writeFd, ix * _kBytesPerBlock);
	osp_assert([_streamFile.blocks count] == ix - 1);
	NSLog(@"=p=streambuf failed restoring ix=%d fileid=%d",
	      ix, _streamFile.fileId);
    }

    // add new tail block
    block = [[MFANAqStreamBlock alloc] initWithBuffer: self];
    block.baseMs = baseMs;


    // see comment on 'if' above to see why in either failure or
    // success case, the next block is at the offset computed below.
    block.fileOffset = ix * _kBytesPerBlock;
    _fileSize = block.fileOffset + _kBytesPerBlock;

    _lastPacketEndMs = block.baseMs;	// no packets in it yet

    [self debugFullCheck];

    pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);

    return 0;
}

NSString *fileNameForFileId(uint32_t fileId) {
    NSString *entryName = [NSString stringWithFormat: @"station-stream-%d.dat", fileId];
    NSString *fileName = fileNameForFile(entryName);

    return fileName;
}

- (NSString *) entryNameForFileId: (uint32_t) fileId {
    NSString *entryName = [NSString stringWithFormat: @"station-stream-%d.dat", fileId];
    return entryName;
}

NSString *altFileNameForFileId(uint32_t fileId) {
    NSString *entryName = [NSString stringWithFormat: @"station-gc-%d.dat", fileId];
    NSString *fileName = fileNameForFile(entryName);

    return fileName;
}

+ (void) cleanupFileId: (uint32_t) fileId {
    NSError *error;
    NSString *filePath;
    BOOL status;

    filePath = fileNameForFileId(fileId);
    status = [[NSFileManager defaultManager] removeItemAtPath: filePath error: &error];
    if (!status) {
	NSLog(@"failed to delete main file=%@ for fileId=%d", filePath, fileId);
    }

    filePath = altFileNameForFileId(fileId);
    status = [[NSFileManager defaultManager] removeItemAtPath: filePath error: &error];
    // alt file rarely exists
}

- (int32_t) readHeaderBlock {
    MFANAqStreamBlockHolder diskBlock;
    char *datap = diskBlock.data();
    int32_t bytesRead;
    uint16_t shortTemp;

    lseek(_streamFile.readFd, 0, SEEK_SET);
    bytesRead = (int32_t) read(_streamFile.readFd, datap, _kBytesPerBlock);
    NSLog(@"readheaderblock read %d bytes", bytesRead);
    if (bytesRead <= 0) {
	NSLog(@"readheaderblock short read %d", bytesRead);
	ftruncate(_streamFile.writeFd, 0);
	return -1;
    }

    memcpy(&shortTemp, datap, 2);
    if (shortTemp != _kFileMagic) {
	NSLog(@"readheaderblock bad magic 0x%x", shortTemp);
	ftruncate(_streamFile.writeFd, 0);
	return -1;
    }
    datap += 2;

    memcpy(&_dataFormat, datap, sizeof(_dataFormat));
    datap += sizeof(_dataFormat);
    [self deriveDataFormatProperties];

    memcpy(&shortTemp, datap, 2);
    datap += 2;
    if (shortTemp == 7) {
	memcpy(_adtsHeader, datap, 7);
    } else {
	memset(_adtsHeader, 0, sizeof(_adtsHeader));
    }
    datap += shortTemp;

    return 0;
}

- (int32_t) updateHeaderBlock {
    MFANAqStreamBlockHolder diskBlock;
    int32_t bytesWritten;
    uint16_t shortTemp;

    char *datap = diskBlock.data();
    char *writeDatap = datap;
    memset(datap, 0, _kBytesPerBlock);

    shortTemp = _kFileMagic;
    memcpy(datap, &shortTemp, sizeof(shortTemp));
    datap += 2;

    // information for the whole file's encoding.
    memcpy(datap, &_dataFormat, sizeof(_dataFormat));
    datap += sizeof(_dataFormat);

    // AAC per-record ADTS prototype (with length field zeroed).
    shortTemp = sizeof(_adtsHeader);
    memcpy(datap, &shortTemp, 2);
    datap += 2;
    memcpy(datap, _adtsHeader, sizeof(_adtsHeader));
    datap += sizeof(_adtsHeader);

    lseek(_streamFile.writeFd, 0, SEEK_SET);
    bytesWritten = (int32_t) write(_streamFile.writeFd, writeDatap, _kBytesPerBlock);
    if (bytesWritten != _kBytesPerBlock)
	return -1;
    else
	return 0;
}

- (int32_t) readPacketsFromBlock: (MFANAqStreamBlock *) block duration: (uint32_t *) durationMsp {
    uint16_t shortTemp;
    uint32_t longTemp;
    char *tdatap;
    uint32_t packetCount=0;
    uint64_t packetStartMs;
    uint32_t durationMs;
    MFANAqStreamBlockHolder diskBlock;
    char *datap;
    int32_t bytesRead;

    NSLog(@"=p= reading block at o=%llx base=%lld dur=%llu",
	  block.fileOffset, block.baseMs, block.durationMs);
    osp_assert(block.sealed && !block.dirty);

    lseek(_streamFile.readFd, block.fileOffset, SEEK_SET);

    packetStartMs = block.baseMs;
    durationMs = 0;
    datap = diskBlock.data();
    bytesRead = (int32_t) read(_streamFile.readFd, datap, _kBytesPerBlock);
    NSLog(@"readpacketsfromblock read %d bytes", bytesRead);
    if (bytesRead <= 0)
	return -1;

    while(bytesRead > 0) {
	if (bytesRead < 2)
	    return -1;
	memcpy(&shortTemp, datap, 2);
	bytesRead -= 2; datap += 2;

	if (shortTemp == _kTrailerMagic) {
	    // block should end with a trailer magic
	    NSLog(@"=p= read success (no inconsistency) with packetcount=%d B", packetCount);
	    if (durationMsp != nullptr)
		*durationMsp = durationMs;
	    return 0;
	}

	if ((shortTemp & _kMagicMask) != _kMagic) {
	    NSLog(@"=p= read inconsistency after %d packets C", packetCount);
	    return -2;
	}
	uint16_t magicFlags = getMagicFlags(shortTemp);

	// packet data count
	if (bytesRead < 2)
	    return -1;
	memcpy(&shortTemp, datap, 2);
	bytesRead -= 2; datap += 2;

	// sanity check data length field
	if (shortTemp > bytesRead) {
	    NSLog(@"=p= read inconsistency after %d packets E", packetCount);
	    return -2;
	}

	MFANAqStreamPacket *packet = [[MFANAqStreamPacket alloc] init];

	// remember the flags in the magic # field
	packet.flags = magicFlags;

	// copy out the packet data into the packet
	[packet setData: std::string(datap, shortTemp)];
	bytesRead -= shortTemp; datap += shortTemp;

	// copy out duration in milliseconds.
	if(bytesRead < 4)
	    return -1;
	memcpy(&longTemp, datap, 4);
	bytesRead -= 4; datap += 4;

	// we know where the block started (and keep a rolling update
	// of the packet start times in packetStartMs).  Update the
	// next packet start time based on this packet's duration.
	packet.startMs = packetStartMs;
	packet.durationMs = longTemp;
	packetStartMs += longTemp;
	durationMs += longTemp;

	// Now read the packet descriptor.  If the packet descriptor size
	// changes, we abort the reading.
	if (bytesRead < 2)
	    return -1;
	memcpy(&shortTemp, datap, 2);
	bytesRead -= 2; datap += 2;

	if (shortTemp > bytesRead ||
	    shortTemp != sizeof(packet.descr))
	    return -1;
	memcpy([packet getDescrAddr], datap, shortTemp);
	bytesRead -= shortTemp; datap += shortTemp;

	if (bytesRead < 2)
	    return -1;
	memcpy(&shortTemp, datap, 2);
	bytesRead -= 2; datap += 2;

	if (shortTemp > 0) {
	    tdatap = (char *) malloc(shortTemp+1);	// extra for added null termination
	    memcpy(tdatap, datap, shortTemp);
	    tdatap[shortTemp] = 0;	// null terminate
	    packet.playingSong = [NSString stringWithUTF8String: tdatap];
	    free(tdatap);
	} else {
	    packet.playingSong = @"";
	}
	bytesRead -= shortTemp; datap += shortTemp;

	// we have a complete packet, now append it; timestamps are
	// already present.
	[block.packetArray addObject: packet];
	packetCount++;
    } // loop over all packets

    // ran out of bytes before the trailer was encountered
    return -3;
}

// Disk format:
//
// 2 bytes -- Magic number _kMagic
//
// 2 bytes -- data length
//
// N bytes -- data
//
// 4 bytes -- packet duration in milliseconds
//
// 2 bytes -- size of AudioStreamPacketDscription
//
// N bytes -- raw packet description.
//
// 2 bytes -- length of playing song (may be zero)
//
// N bytes -- song name as UTF-8 bytes
//
// When all the records have been written, we write a trailer record consisting of:
//
// 2 bytes -- _kTrailerMagic.
- (int32_t) writePacketsToBlock: (MFANAqStreamBlock *) block {
    int fd;
    size_t code;
    uint16_t shortTemp;
    uint32_t longTemp;
    MFANAqStreamBlockHolder diskBlock;

    NSLog(@"writing block at offset %llx", block.fileOffset);
    // opens an existing file for read and write without truncating it
    fd = _streamFile.writeFd;
    lseek(fd, block.fileOffset, SEEK_SET);

    MFANAqStreamPacket *packet;
    char *datap = diskBlock.data();

    [self debugCheck: block];
    for(packet in block.packetArray) {
	// write out magic # with flags
	shortTemp = setMagicFlags(packet.flags);
	memcpy(datap, &shortTemp, 2);
	datap += 2;

	// write out data length
	shortTemp = [packet getLength];
	memcpy(datap, &shortTemp, 2);
	datap += 2;

	// write out data
	memcpy(datap, [packet getData], shortTemp);
	datap += shortTemp;

	longTemp = (uint32_t) packet.durationMs;
	memcpy(datap, &longTemp, 4);
	datap += 4;

	shortTemp = sizeof(AudioStreamPacketDescription);
	memcpy(datap, &shortTemp, 2);
	datap += 2;

	memcpy(datap, [packet getDescrAddr], shortTemp);
	datap += shortTemp;

	shortTemp = (uint16_t) [packet.playingSong length];
	memcpy(datap, &shortTemp, 2);
	datap += 2;

	if (shortTemp > 0) {
	    memcpy(datap,
		   [packet.playingSong cStringUsingEncoding: NSUTF8StringEncoding],
		   shortTemp);
	    datap += shortTemp;
	}
    } // loop over all records in this block

    // write end
    shortTemp = _kTrailerMagic;
    memcpy(datap, &shortTemp, 2);

    code = write(fd, diskBlock.data(), _kBytesPerBlock);
    if (code != _kBytesPerBlock) {
	NSLog(@"write failure -- write %ld", code);
	return -1;
    }

    return 0;
}

- (void) debugFullCheck {
#if 0
    uint32_t i;
    uint32_t blockCount = (uint32_t) [_streamFile.blocks count];
    MFANAqStreamBlock *block;

    osp_assert (blockCount > 0);
    for(i=0;i<blockCount-1;i++) {
	block = _streamFile.blocks[i];
	uint64_t packetArrayCount = [block.packetArray count];
	if (packetArrayCount > 0) {
	    MFANAqStreamPacket *p = block.packetArray[packetArrayCount - 1];;
	    osp_assert(block.baseMs + block.durationMs == p.startMs + p.durationMs);
	}
	if (i < blockCount - 2) {
	    // don't check unsealed active block.
	    MFANAqStreamBlock *nextBlock;
	    nextBlock = _streamFile.blocks[i+1];
	    osp_assert(nextBlock.baseMs == block.baseMs + block.durationMs);
	}
    }
#endif
}

- (void) debugCheck: (MFANAqStreamBlock *) block {
    uint64_t packetCount = [block.packetArray count];
    if (block.baseMs > 0 && packetCount > 0) {
	MFANAqStreamPacket *packet = block.packetArray[packetCount-1];
	osp_assert(block.baseMs + block.durationMs == packet.startMs + packet.durationMs);
    }
}

- (pthread_cond_t *) packetArrayCv {
    return &_packetArrayCv;
}

- (uint32_t) packetCount {
    uint32_t count;
    pthread_mutex_lock(&_bufferMutex);
    count = (uint32_t) [_packetArray count];
    pthread_mutex_unlock(&_bufferMutex);

    return count;
}

- (MFANAqStreamPacket *) lastPacket {
    MFANAqStreamPacket *packet = nil;

    pthread_mutex_lock(&_bufferMutex);
    if ([_packetArray count] > 0) {
	packet = [_packetArray lastObject];
    }
    pthread_mutex_unlock(&_bufferMutex);

    return packet;
}

- (void) commonInitWithFileId: (uint32_t) fileId {
    MFANAqStreamBlock *block;

    if (!_bufferStaticSetup) {
	pthread_mutex_init(&_bufferMutex, NULL);
	_bufferStaticSetup = YES;
    }
    pthread_mutex_init(&_blockMutex, nullptr);
    pthread_cond_init(&_packetArrayCv, NULL);
    pthread_cond_init(&_pthreadReqCv, NULL);
    pthread_cond_init(&_pthreadDoneCv, NULL);
    pthread_cond_init(&_blockIoCv, NULL);

    _packetArray = [[NSMutableOrderedSet alloc] init];
    _haveProperties = NO;
    _lastPacketEndMs = 0;
    _packetDuration = 0.0;
    _frameDuration = 0.0;
    _shuttingDown = false;
    _pthreadDone = false;
    _gcRunning = false;
    _pthreadDoWork = false;
    _validBlocks = 0;
    _dirtyBlocks = 0;
    _fileSize = 2*_kBytesPerBlock;
    memset(&_adtsHeader, 0, sizeof(_adtsHeader));

    _streamFile = [[MFANAqStreamFile alloc] init];

    osp_assert(fileId != 0 && fileId != ~0U);	// debug
    _streamFile.fileId = fileId;

    // create first block so we have somewhere to put data.
    // Invariant is that this block always exists, but isn't in
    // the LRU and isn't marked as dirty until it is complete.
    block = [[MFANAqStreamBlock alloc] initWithBuffer: self];

    _streamBufferThread = [[NSThread alloc] initWithTarget: self
						  selector: @selector(ioAsync:)
						    object: nil];
    [_streamBufferThread start];
}

- (void) openFilesForFileId: (uint32_t) fileId {
    // open and create the backing file
    int fd = open([fileNameForFileId(_streamFile.fileId)
		      cStringUsingEncoding: NSUTF8StringEncoding],
		  O_CREAT | O_RDWR, 0666);
    osp_assert(fd >= 0);
    _streamFile.writeFd = fd;

    fd = open([fileNameForFileId(_streamFile.fileId)
		  cStringUsingEncoding: NSUTF8StringEncoding], O_RDONLY);
    osp_assert(fd >= 0);
    _streamFile.readFd = fd;
}

- (MFANAqStreamBuffer *) initWithFileId: (uint32_t) fileId {
    self = [super init];
    if (self != nil) {
	[self commonInitWithFileId: fileId];

	[self openFilesForFileId: fileId];
    }

    return self;
}

- (MFANAqStreamBuffer *) initWithNewFileId: (uint32_t) newFileId
				 oldBuffer: (MFANAqStreamBuffer *) oldBuffer {
    int32_t code;

    self = [super init];
    if (self != nil) {
	[self commonInitWithFileId: newFileId];

	// open and create the backing file
	[self openFilesForFileId: _streamFile.fileId];

	// now copy data from old file to new
	MFANAqStreamBlockHolder diskBlock;
	lseek(oldBuffer->_streamFile.readFd, 0, SEEK_SET);
	lseek(_streamFile.writeFd, 0, SEEK_SET);
	while(true) {
	    code = (int32_t) read(oldBuffer->_streamFile.readFd,
				  diskBlock.data(),
				  _kBytesPerBlock);
	    if (code <= 0)
		break;
	    code = (int32_t) write(_streamFile.writeFd,
				   diskBlock.data(),
				   _kBytesPerBlock);
	    if (code < _kBytesPerBlock) {
		NSLog(@"Snapshot COPY FAILED");
		return nil;
	    }
	}
	fsync(_streamFile.writeFd);

	[self restoreBlocksFromFile];
    }

    return self;
}

- (void) dealloc {
    // cleanup c++ allocated structures
    if (_streamFile) {
	// ARC doesn't know to walk into streamFile to find references
	// to release.
	_streamFile.blocks = nil;
	_streamFile.lru = nil;
	_streamFile = nullptr;
    }
}

- (MFANAqStreamFile *) getStreamFile {
    return _streamFile;
}

- (void) fillBlock: (MFANAqStreamBlock *) block {
    while(true) {
	if (!block.ioRunning)
	    break;
	pthread_cond_wait(&_blockIoCv, &_bufferMutex);
    }

    if ([block validContents])
	return;

    block.ioRunning = true;

    // do the IO without the lock, after setting ioRunning flag.
    [self readPacketsFromBlock: block duration: nullptr];
    [self debugCheck: block];

    NSLog(@"=p=fill done baseMs=%lld o=%llx pkts=%ld", block.baseMs,
	  block.fileOffset, (long) [block.packetArray count]);
    block.ioRunning = false;
    osp_assert(!block.valid);
    block.valid = true;
    _validBlocks++;
    [self fixLru: block];
    pthread_cond_broadcast(&_blockIoCv);
}

- (void) invalidateBlock: (MFANAqStreamBlock *) block {
    while(true) {
	if (block.ioRunning) {
	    pthread_cond_wait(&_blockIoCv, &_bufferMutex);
	    continue;
	} else {
	    break;
	}
    }

    if (block.valid) {
	block.valid = false;
	_validBlocks--;
    }

    if (block.dirty) {
	block.dirty = false;
	_dirtyBlocks--;
    }

    [self fixLru: block];
}

- (void) cleanBlock: (MFANAqStreamBlock *) block
	       isGc: (bool) isGc {
    // wait for both GC to finish and for block IO to be off
    while(true) {
	if (block.ioRunning) {
	    pthread_cond_wait(&_blockIoCv, &_bufferMutex);
	    continue;
	}

	if (!isGc) {
	    if (_gcRunning) {
		pthread_cond_wait(&_pthreadDoneCv, &_bufferMutex);
		continue;
	    }
	}

	break;
    }

    if (!block.dirty)
	return;

    // once we set ioRunning, no one else should turn off _dirty
    block.ioRunning = true;
    NSLog(@"=p= cleaning block o=%llx base=%lld dur=%lld pkts=%lu",
          block.fileOffset, block.baseMs, block.durationMs,
	  (unsigned long)[block.packetArray count]);
    [self writePacketsToBlock: block];
    NSLog(@"=p= clean done block o=%llx base=%lld", block.fileOffset, block.baseMs);

    // allow new IOs to start
    block.ioRunning = false;
    block.dirty = false;
    osp_assert(_dirtyBlocks > 0);
    _dirtyBlocks--;
    pthread_cond_broadcast(&_blockIoCv);
}

- (void) abortGc: (int32_t) code {
    close(_gcNewFd);
    close(_gcOldFd);
    _gcRunning = false;
}

- (void) gcAsync: (id) junk {
    int32_t code;
    MFANAqStreamBlockHolder diskBlock;
    MFANAqStreamBlock *block;
    uint32_t blockIx;

    NSLog(@"=g= GC starts");
    pthread_mutex_lock(&_blockMutex);
    pthread_mutex_lock(&_bufferMutex);

    _gcRunning = true;
    NSString *newFileName = altFileNameForFileId (_streamFile.fileId);
    _gcNewFd = open([newFileName cStringUsingEncoding: NSUTF8StringEncoding],
		    O_CREAT | O_WRONLY | O_TRUNC, 0666);
    osp_assert(_gcNewFd >= 0);

    NSString *oldFileName = fileNameForFileId (_streamFile.fileId);
    _gcOldFd = open([oldFileName cStringUsingEncoding: NSUTF8StringEncoding], O_RDONLY);
    osp_assert(_gcOldFd >= 0);

    // Copy the metadata header block
    lseek(_gcOldFd, 0, SEEK_SET);
    // new file was just created empty
    code = (int32_t) read(_gcOldFd, diskBlock.data(), _kBytesPerBlock);
    if (code < _kBytesPerBlock) {
	NSLog(@"=g= GC can't read header block");
	[self abortGc: code];
	pthread_mutex_unlock(&_bufferMutex);
	pthread_mutex_unlock(&_blockMutex);
	return;
    }
    code = (int32_t) write(_gcNewFd, diskBlock.data(), _kBytesPerBlock);
    if (code < _kBytesPerBlock) {
	NSLog(@"=g= GC can't write header block");
	[self abortGc: code];
	pthread_mutex_unlock(&_bufferMutex);
	pthread_mutex_unlock(&_blockMutex);
	return;
    }

    // the first block will appear at offset kBytesPerBlock of the new
    // file, so its offset is the shift.
    uint64_t gcByteShift = _streamFile.blocks[0].fileOffset;
    osp_assert(gcByteShift >= _kBytesPerBlock);
    gcByteShift -= _kBytesPerBlock;
    _streamFile.gcBlockShift = (uint32_t) gcByteShift / _kBytesPerBlock;

    NSLog(@"=g= starting GC block count=%ld removing %d blocks",
	  (long) [_streamFile.blocks count], _streamFile.gcBlockShift);

    BOOL failed = false;

    // The concurrency issues are a little tricky, since _blocks can
    // be growing at the end during this time.  But there can be no
    // blocks added from the time blockIx reaches the end and the time
    // we finally swap the new file for the old.  So, the whole thing
    // looks like an atomic change.
    //
    // Note that the file offset changes, but the ms label doesn't.
    uint64_t expectedOffset;
    expectedOffset = _streamFile.blocks[0].fileOffset;

    // make sure we evaluate block.count on every spin around, to catch new
    // blocks added.
    for(blockIx = 0; blockIx < [_streamFile.blocks count] - 1; blockIx++) {
	block = _streamFile.blocks[blockIx];
	osp_assert(block.fileOffset == expectedOffset);
	expectedOffset += _kBytesPerBlock;
	// the last block may not be sealed (typically isn't)
	if (!block.sealed) {
	    osp_assert(blockIx == [_streamFile.blocks count] - 1);
	    break;
	}

	if (block.dirty) {
	    NSLog(@"=p= GC cleaning block off=%llx base=%lld", block.fileOffset, block.baseMs);
	    [self cleanBlock: block isGc:true ]; // so there'll be something to copy.
	}

	while (block.ioRunning) {
	    pthread_cond_wait(&_blockIoCv, &_bufferMutex);
	    NSLog(@"=g= waiting for iorunning bix=%d base=%lld",
		  blockIx, block.baseMs);
	}
	block.ioRunning = true;

	// copy both valid and non-valid blocks, since valid just
	// means that the memory data is not valid.
	pthread_mutex_unlock(&_bufferMutex);

	// now copy the block
	lseek(_gcOldFd, block.fileOffset, SEEK_SET);
	NSLog(@"=p= GC reading block off=%llx base=%lld", block.fileOffset, block.baseMs);
	code = (int32_t) read(_gcOldFd, diskBlock.data(), _kBytesPerBlock);
	if (code < _kBytesPerBlock) {
	    if (code < 0) {
		// may get short read on last sealed block
		NSLog(@"=g= GC read failure at offset %llx", block.fileOffset);
		failed = true;
		block.ioRunning = false;
		break;
	    } else {
		NSLog(@"=g= GC short read code=%d bix=%d base=%lld o=%llx",
		      code, blockIx, block.baseMs, block.fileOffset);
		osp_assert(blockIx >= [_streamFile.blocks count]-1);
	    }
	}

	lseek(_gcNewFd, block.fileOffset - gcByteShift, SEEK_SET);
	code = (int32_t) write(_gcNewFd, diskBlock.data(), _kBytesPerBlock);
	if (code != _kBytesPerBlock) {
	    NSLog(@"=g= GC write failure");
	    block.ioRunning = false;
	    failed = true;
	    break;
	}

	NSLog(@"=g=p= GC moved block from o=%llx to o=%llx base=%lld dur=%lld bix=%u",
	      block.fileOffset, block.fileOffset-gcByteShift,
	      block.baseMs, block.durationMs, blockIx);

	// and relock now that the work is done.
	pthread_mutex_lock(&_bufferMutex);

	block.ioRunning = false;
	pthread_cond_broadcast(&_blockIoCv);
    }

    // in case we took an error path
    pthread_cond_broadcast(&_blockIoCv);

    // At this point, all we have left to do is a rename and update
    // the offsets

    // we're done with the open files.
    close(_gcOldFd);

    fsync(_gcNewFd);
    close(_gcNewFd);

    code = rename([newFileName cStringUsingEncoding: NSUTF8StringEncoding],
		  [oldFileName cStringUsingEncoding: NSUTF8StringEncoding]);
    osp_assert(code == 0);

    blockIx = 0;
    for(block in _streamFile.blocks) {
	// offsets in GC file are correct for offsets in regular file after
	// rename of GC file to be the main file.
	osp_assert(block.fileOffset >= gcByteShift);
	block.fileOffset -= gcByteShift;
	NSLog(@"=g= adjusted block bix=%d base=%lld oldo=%llx newo=%llx",
	      blockIx, block.baseMs, block.fileOffset+gcByteShift, block.fileOffset);
	blockIx++;
    }

    // close files and reopen new one
    if (_streamFile.readFd >= 0) {
	close(_streamFile.readFd);
	_streamFile.readFd = -1;
    }
    if (_streamFile.writeFd >= 0) {
	close(_streamFile.writeFd);
	_streamFile.writeFd = -1;
    }
    [self openFilesForFileId: _streamFile.fileId];

    NSLog(@"=g=p= GC done");
    _gcRunning = false;

    pthread_mutex_unlock(&_bufferMutex);
    pthread_mutex_unlock(&_blockMutex);
    pthread_cond_broadcast(&_pthreadDoneCv);
}

- (void) ioAsync: (id) junk {
    MFANAqStreamBlock *block;

    pthread_mutex_lock(&_bufferMutex);
    while(true) {
	@autoreleasepool {
	    if (_shuttingDown)
		break;

	    if (_pthreadDoWork) {
		// clear flag telling us there's work to do.
		_pthreadDoWork = false;

		// see if we should pull some buffers from the LRU queue
		// and remove their data.
		while (_validBlocks > _kMaxValidBlocks) {
		    block = _streamFile.lru[0];
		    if (block.dirty) {
			[self cleanBlock: block isGc: false];
			NSLog(@"cleaned block for invalidation at %llx ms=%lld",
			      block.fileOffset, block.baseMs);
		    }

		    // cleaning drops lock, so buffer might no longer be
		    // valid
		    osp_assert(block.sealed);
		    if (block.valid) {
			osp_assert(!block.dirty);
			[block.packetArray removeAllObjects];
			block.valid = false;
			osp_assert(_validBlocks > 0);
			_validBlocks--;
			[self fixLru: block];
			NSLog(@"invalidated block at off=%llx ms=%lld",
			      block.fileOffset, block.baseMs);
		    }
		}

		// see if we can find dirty blocks near the end of the
		// file, and clean them.  Don't clean the last one, since
		// it is still accumulating new packets, and of course
		// don't wait for the last one to get cleaned, either.
		uint32_t blockCount = (uint32_t) [_streamFile.blocks count];
		while(_dirtyBlocks > 1) {
		    bool foundAny = false;
		    for(int32_t i=blockCount - 2; i >= 0; i--) {
			block = _streamFile.blocks[i];
			if (block.dirty) {
			    [self cleanBlock: block isGc:false];
			    foundAny = true;
			    NSLog(@"cleaned block in background at %llx", block.fileOffset);
			}
		    }

		    // in case dirtyBlocks is incorrect
		    if (!foundAny)
			break;
		}
	    } else {
		// wait for new request
		pthread_cond_wait(&_pthreadReqCv, &_bufferMutex);
	    }
	}
    }
    _pthreadDone = true;
    pthread_mutex_unlock(&_bufferMutex);
    pthread_cond_broadcast(&_pthreadDoneCv);
}

- (void) abortReaders {
    pthread_mutex_lock(&_bufferMutex);
    _shuttingDown = YES;
    pthread_mutex_unlock(&_bufferMutex);

    // and wake anyone waiting.
    pthread_cond_broadcast(&_packetArrayCv);
}

- (void) allowReaders {
    pthread_mutex_lock(&_bufferMutex);
    _shuttingDown = NO;
    pthread_mutex_unlock(&_bufferMutex);

    // and wake anyone waiting.
    pthread_cond_broadcast(&_packetArrayCv);
}

- (void) erase {
    MFANAqStreamBlock *newBlock;
    uint32_t loops;

    // remove everything, but preserve the first block, since until
    // you start the stream again, it has the only copy of the stream
    // parameters we need to be able to play newly arriving data.
    pthread_mutex_lock(&_blockMutex);
    pthread_mutex_lock(&_bufferMutex);

    // We need to remove all the blocks, but there can be threads with
    // references to these blocks, so we have to invalidate the blocks
    // one at a time so we can do the appropriate cleanup on them.
    // The flags dirty, valid, ioRunning and inLru must all be cleaned
    // up individually.  We may need to block on blockIoCv and release
    // the lock to clear ioRunning.
    //
    // We will need to wait for the _blockMutex that protects changes
    // to the blocks array (except for adding new blocks to the end)
    // and copying operations on the data file itself.
    for(loops = 0; loops < 100; loops++) {
	// code works for block at end as well
	uint32_t blockCount = (uint32_t) [_streamFile.blocks count];
	uint32_t i;
	MFANAqStreamBlock *block;
	bool didWork;

	didWork = false;
	for(i=0;i<blockCount;i++) {
	    block = _streamFile.blocks[i];
	    if (block.dirty || block.valid || block.ioRunning) {
		[self invalidateBlock: block];
		didWork = true;
	    }
	}
	if (!didWork)
	    break;
    }
    osp_assert(loops < 100);
    [_streamFile.blocks removeAllObjects];
    ftruncate(_streamFile.writeFd, _kBytesPerBlock);	// leave header block alone

    // reset global state
    _dirtyBlocks = 0;
    _validBlocks = 0;
    _fileSize = _kBytesPerBlock;
    _firstPacketStartMs = 0;
    _lastPacketEndMs = 0;

    // and create new block for incoming data.  Code assumes there's always
    // one block in the blocks array
    newBlock = [[MFANAqStreamBlock alloc] initWithBuffer: self];
    newBlock.baseMs = 0;
    newBlock.fileOffset = _kBytesPerBlock;

    pthread_mutex_unlock(&_bufferMutex);
    pthread_mutex_unlock(&_blockMutex);
}

// prune records so that all records older than pruneLength ms before
// the last appended packet get deleted.
- (void) pruneOldestMs: (uint64_t) pruneLength {
    uint64_t startMs;
    MFANAqStreamBlock *block;
    uint32_t blockCount;

    pthread_mutex_lock(&_blockMutex);
    pthread_mutex_lock(&_bufferMutex);
    if (_gcRunning || pruneLength > _lastPacketEndMs) {
	pthread_mutex_unlock(&_bufferMutex);
	pthread_mutex_unlock(&_blockMutex);
        return;
    } else {
        startMs = _lastPacketEndMs - pruneLength;
    }

    // Remove whole blocks, since each block is perhaps 0.5 - 2.0 seconds, and
    // that's good enough.
    while (true) {
	// be careful never to remove the last block, since the code
	// in this module assumes there's always at least one block in
	// the array.
	blockCount = (uint32_t) [_streamFile.blocks count];
	if (blockCount > 1) {
	    while(true) {
		block = _streamFile.blocks[0];
		if (!block.ioRunning)
		    break;
		pthread_cond_wait(&_blockIoCv, &_bufferMutex);
	    }
	    if (block.baseMs < startMs) {
		NSLog(@"=g=p= prune removing block base=%lld dur=%lld o=%llx bix=0 pkts=%ld",
		      block.baseMs, block.durationMs, block.fileOffset,
		      (long) [block.packetArray count]);
		// this will free all the data in memory, but the disk
		// file will still need to be compacted eventually.
		[_streamFile.blocks removeObjectAtIndex: 0];

		// someone other thread may have a reference to block,
		// so make sure we mark it as invalid and clean.
		if (block.valid) {
		    _validBlocks--;
		    block.valid = false;
		    [self fixLru: block];
		}
		if (block.dirty) {
		    _dirtyBlocks--;
		    block.dirty = false;
		}
	    } else {
		break;
	    }
	} else
	    break;
    }

    blockCount = (uint32_t) [_streamFile.blocks count];
    if (blockCount <= 0) {
	NSLog(@"=g= pruneOldest no blocks left");
	_firstPacketStartMs = 0;
    } else {
	block = _streamFile.blocks[0];
	_firstPacketStartMs = block.baseMs;
	NSLog(@"=g= pruneOldest first block now o=%llx base=%lld bix=0",
	      block.fileOffset, block.baseMs);
    }

    // Now check to see if we need to start a GC.  We start one if the file is more
    // than 50% garbage.  We can tell by seeing how many blocks are in the file,
    // and where the first block's file offset is.
    if ( !_gcRunning && !_shuttingDown &&
	 (100 * (_streamFile.blocks[0].fileOffset / _kBytesPerBlock) / blockCount >
	  _kMaxDiskPct)) {
	_gcRunning = true;
        _gcBufferThread = [[NSThread alloc] initWithTarget: self
						  selector: @selector(gcAsync:)
						    object: nil];
	[_gcBufferThread start];
    }
    pthread_mutex_unlock(&_bufferMutex);
    pthread_mutex_unlock(&_blockMutex);
}

// Finalize the info for the block that was being appended to, by
// adding it to the LRU queue.  Then initialize the new block from
// info in the previous.
//
// Note that this creates a new unselaed block at the end which isn't
// marked as valid or dirty, nor counted as either.  Because it isn't
// valid, it isn't in the _lru.  But it *is* at the end of the _blocks
// array.
- (MFANAqStreamBlock *) addBlockAndSealPrev {
    MFANAqStreamBlock *newBlock;
    MFANAqStreamBlock *prevBlock;
    prevBlock = [_streamFile.blocks lastObject];

    newBlock = [[MFANAqStreamBlock alloc] initWithBuffer: self];

    // note that there's always a prevBlock since we initialize this
    // with an empty block and we never delete the last block.
    prevBlock.sealed = true;
    osp_assert(!prevBlock.valid);
    prevBlock.valid = true;
    _validBlocks++;
    [self fixLru: prevBlock];

    // mark the sealed block as dirty
    osp_assert(!prevBlock.dirty);
    prevBlock.dirty = true;
    _dirtyBlocks++;
    NSLog(@"=p=g= sealed block o=%llx bix=%ld base=%lld dur=%lld pkts=%ld",
	  prevBlock.fileOffset, [_streamFile.blocks count]-1,
	  prevBlock.baseMs, prevBlock.durationMs, [prevBlock.packetArray count]);

    newBlock.baseMs = prevBlock.durationMs + prevBlock.baseMs;
    newBlock.fileOffset = prevBlock.fileOffset + _kBytesPerBlock;
    // caller will add newBLock to array at count offset
    NSLog(@"addBlockAndSeal sealed block startMs=%lld %lld packets newBlock startMs=%lld bix=%llu",
	  prevBlock.baseMs, (uint64_t) [prevBlock.packetArray count],
	  newBlock.baseMs, (uint64_t) [_streamFile.blocks count]);
    _fileSize += _kBytesPerBlock;

    return newBlock;
}

- (void) fixLru: (MFANAqStreamBlock *) block {
    bool needLru;
    if (block.pinCount > 0)
	needLru = false;
    else if (block.valid) {
	needLru = true;
    } else {
	needLru = false;
    }

    if (needLru && !block.inLru) {
	[_streamFile.lru addObject: block];
	block.inLru = true;
    } else if (!needLru && block.inLru) {
	[_streamFile.lru removeObject: block];
	block.inLru = false;
    }
}

- (void) addPacket: (MFANAqStreamPacket *) packet withDuration: (uint32_t) durationMs {
    MFANAqStreamBlock *block;
    uint32_t packetBytesUsed;

    pthread_mutex_lock(&_bufferMutex);

    packet.startMs = _lastPacketEndMs;
    packet.durationMs = durationMs;
    packetBytesUsed = [self packetDiskSize: packet];

    _lastPacketEndMs = packet.startMs + durationMs;
    block = [self lastBlockSetIndex: nullptr];
    if (block.diskBytesUsed + packetBytesUsed > _kBytesPerBlock - _kTrailerBytes) {
	block = [self addBlockAndSealPrev];
	NSLog(@"addpacket new block");

	// wakeup cleaner and pruner
	pthread_cond_broadcast(&_pthreadReqCv);
    }

    [block.packetArray addObject: packet];

    block.diskBytesUsed += packetBytesUsed;
    block.durationMs += packet.durationMs;
    _pthreadDoWork = true;
    pthread_mutex_unlock(&_bufferMutex);

    // wakeup anyone waiting for more data to read
    pthread_cond_broadcast(&_packetArrayCv);
}

- (void) deriveDataFormatProperties {
    _frameDuration  = 1.0f / _dataFormat.mSampleRate;
    _packetDuration = _dataFormat.mFramesPerPacket / _dataFormat.mSampleRate;
    _haveProperties = YES;

    NSLog(@"frame duration=%f packetDuration=%f",
	  _frameDuration, _packetDuration);
}

- (void) setDataFormat: (AudioStreamBasicDescription *) descr
	    adtsHeader: (char *) headerp {
    _dataFormat = *descr;
    memcpy(_adtsHeader, headerp, sizeof(_adtsHeader));

    [self deriveDataFormatProperties];
    [self updateHeaderBlock];
}

- (NSString *) getDataFormatString {
    NSString *rstr;

    if (_dataFormat.mFormatID == 'aac ')
        rstr = @"AAC";
    else if (_dataFormat.mFormatID == '.mp3')
        rstr = @"MP3";
    else {
        /* unknown, just use the raw encoding as characters */
        rstr = [NSString stringWithFormat: @"%c%c%c%c",
                 (int) (_dataFormat.mFormatID>>24) & 0xFF,
                 (int) (_dataFormat.mFormatID>>16) & 0xFF,
                 (int) (_dataFormat.mFormatID>>8) & 0xFF,
                 (int) _dataFormat.mFormatID & 0xFF];
    }
    return rstr;
}

- (void) shutdown {
    pthread_mutex_lock(&_bufferMutex);
    _shuttingDown = true;
    while(true) {
	pthread_cond_broadcast(&_pthreadReqCv);
	if (_pthreadDone && !_gcRunning) break;
	pthread_cond_wait(&_pthreadDoneCv, &_bufferMutex);
    }
    pthread_mutex_unlock(&_bufferMutex);
}

- (void) getDataFormat: (AudioStreamBasicDescription *) format {
    *format = _dataFormat;
}

- (NSMutableData *) getAdtsHeaderForLength: (int32_t) rawLen {
    char *datap;
    uint32_t len = rawLen + sizeof(_adtsHeader);
    NSMutableData *data = [[NSMutableData alloc]
			      initWithBytes: _adtsHeader
				     length: sizeof(_adtsHeader)];
    datap = (char *) [data mutableBytes];
    datap[1] |= 0x1;	// turn on 'no crc' flag
    datap[3] |= (len >> 11) & 0x3F;
    datap[4] = (len >> 3) & 0xFF;	// replacing all bits in byte
    datap[5] |= (len & 0x7) << 5;
    return data;
}

@end
