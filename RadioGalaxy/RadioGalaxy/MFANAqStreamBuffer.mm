#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#import "MFANAqStreamBuffer.h"
#import "MFANCGUtil.h"

#include "osp.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>

#include <string>

// ---------------------------------------------------------------------------
// MFANAqStreamPacket
// ---------------------------------------------------------------------------

@implementation MFANAqStreamPacket {
    uint64_t _startMs;
    uint32_t _durationMs;

    std::string _data;
    bool _read;
    NSString *_playingSong;
    AudioStreamPacketDescription _descr;
}

- (MFANAqStreamPacket *) init {
    self = [super init];
    if (self != nil) {
        _read = NO;
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

	_closed = NO;

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

// Called with bufferMutex held.
// Returns the index of the first packet with startMs >= ms.
- (uint32_t) findPacketIx: (uint64_t) ms inBlock: (MFANAqStreamBlock *) block {
    uint32_t ix;
    MFANAqStreamPacket *packet;

    // caller should have done this for us
    osp_assert([block validContents]);

    for(ix = 0; ix < [block.packetArray count]; ix++) {
        packet = block.packetArray[ix];
        if (packet.startMs >= ms)
            break;
    }

    NSLog(@"findPacketIx for %lld ms off=%llx returns pix=%u/%u ms=%lld",
	  ms, block.fileOffset, ix, (uint32_t) [block.packetArray count],
	  packet.startMs);
    return ix;
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
    _packetIx = [self findPacketIx: ms inBlock: block];
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
        if (_streamBuffer.shuttingDown || _closed) {
	    // failed, so return false
            break;
        }

	blockCount = (uint32_t) [_streamBuffer.streamFile->_blocks count];

        if (!self.indicesAreValid) {
	    block = [_streamBuffer findBlockAtMs: _recordMs setIndex:&blockIx];
	    packetIx = [self findPacketIx: _recordMs inBlock: block];
        } else {
	    blockIx = _blockIx;
	    packetIx = _packetIx;
	    if (blockIx >= blockCount) {
		pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
		return false;
	    }
	    block = _streamBuffer.streamFile->_blocks[blockIx];
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
	    block = _streamBuffer.streamFile->_blocks[blockIx];
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
    blockCount = (uint32_t) [_streamBuffer.streamFile->_blocks count];

    if (!self.indicesAreValid) {
	block = [_streamBuffer findBlockAtMs: _recordMs setIndex:&blockIx];
	packetIx = [self findPacketIx: _recordMs inBlock: block];
    } else {
	blockIx = _blockIx;
	packetIx = _packetIx;
	if (blockIx >= blockCount)
	    block = nil;
	else
	    block = _streamBuffer.streamFile->_blocks[blockIx];
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

    pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);
    NSLog(@"read starts for recordMs=%lld", _recordMs);
    while(true) {
        // Abort if the buffer itself is being shutdown or the reader
        // was closed.
        if (_streamBuffer.shuttingDown || _closed) {
            pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
	    NSLog(@"read done --> closed");
            return nil;
        }

        if (!self.indicesAreValid) {
	    block = [_streamBuffer findBlockAtMs: _recordMs setIndex:&_blockIx];
	    _packetIx = [self findPacketIx: _recordMs inBlock: block];
	    NSLog(@"read index !valid, using bix=%d pix=%d baseMs=%lld o=%llx (recordMs=%lld)",
		  _blockIx, _packetIx, block.baseMs, block.fileOffset, _recordMs);
        } else {
	    block = _streamBuffer.streamFile->_blocks[_blockIx];
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
	blockCount = (uint32_t) [_streamBuffer.streamFile->_blocks count];

	// blockCount is never 0; it is one upon creation and we never
	// get rid of the block at the end collecting packets.
	lastBlockIx = blockCount - 1;

        if ( _blockIx > lastBlockIx ||
	     (_blockIx == lastBlockIx &&
	      _packetIx >= packetCount)) {
            // No packet at the current index.
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
	    continue;
	}

	// otherwise we can actually return a packet
        packet = block.packetArray[_packetIx];
        _recordMs = packet.startMs + packet.durationMs;
        _packetIx++;
        break;
    }
    pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
    packet.read = YES;
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
	_fileOffset = 0;
	_diskBytesUsed = 0;
	_durationMs = 0;
	_inLru = false;
	_pinCount = 0;
	_packetArray = [[NSMutableArray alloc] init];

	// gets marked dirty and valid when it gets sealed
	[buffer.streamFile->_blocks addObject: self];
    }

    return self;
}

- (BOOL) validContents {
    // valid is never set on the unsealed block at the end.
    return _valid || !_sealed;
}

@end

// ---------------------------------------------------------------------------
// MFANAqStreamBuffer
// ---------------------------------------------------------------------------

static int _bufferStaticSetup = 0;
static pthread_mutex_t _bufferMutex;
static const uint32_t _kBytesPerBlock = 16*1024;
static const uint32_t _kTrailerBytes = 2;
static const uint16_t _kMagic = 0x0301;
static const uint16_t _kTrailerMagic = 0x0924;
static const uint32_t _kMaxValidBlocks = 32;
static const uint32_t _kMaxDiskPct = 50;		// maximum unused disk space before reclaim

@implementation MFANAqStreamBuffer {
    pthread_cond_t _packetArrayCv;	// data arrival CV
    pthread_cond_t _pthreadReqCv;	// pthread should look for work CV
    pthread_cond_t _pthreadDoneCv;	// waiting for pthread exit.
    pthread_cond_t _blockIoCv;		// block fill / clean wait/completed
    MFANAqStreamFile *_streamFile;
    uint64_t _lastPacketEndMs;
    uint64_t _firstPacketStartMs;
    uint64_t _fileSize;

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
}

+ (pthread_mutex_t *) bufferMutex {
    return &_bufferMutex;
}

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
    if (blockIx >= [_streamFile->_blocks count])
	return false;

    // otherwise this is the block to check
    block = _streamFile->_blocks[blockIx];

    if (blockIx >= [_streamFile->_blocks count] - 1)
	nextBlock = nil;
    else
	nextBlock = _streamFile->_blocks[blockIx+1];

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

// these functions not only return a block, but ensure that its
// contents are valid.  The last block is special; it isn't marked as
// valid, but it is always valid and never in the LRU queue.
- (MFANAqStreamBlock *) lastBlockSetIndex: (uint32_t *) indexp {
    // last block is always valid
    uint32_t blockIndex = (uint32_t) [_streamFile->_blocks count] - 1;
    if (indexp != nullptr)
	*indexp = blockIndex;
    return _streamFile->_blocks[blockIndex];
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
	blockCount = (uint32_t) [_streamFile->_blocks count];
	for(i=0;i<blockCount;i++) {
	    block = _streamFile->_blocks[i];
	    if ( ms >= block.baseMs &&
		 ms < block.baseMs + block.durationMs)
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

NSString *fileNameForFileId(uint32_t fileId) {
    NSString *entryName = [NSString stringWithFormat: @"station-stream-%d.dat", fileId];
    NSString *fileName = fileNameForFile(entryName);

    return fileName;
}

NSString *altFileNameForFileId(uint32_t fileId) {
    NSString *entryName = [NSString stringWithFormat: @"station-gc-%d.dat", fileId];
    NSString *fileName = fileNameForFile(entryName);

    return fileName;
}

- (int32_t) readPacketsFromBlock: (MFANAqStreamBlock *) block {
    NSString *fileName = fileNameForFileId(_streamFile->_fileId);
    FILE *filep;
    size_t code;
    uint16_t shortTemp;
    uint32_t longTemp;
    char *tdatap;
    uint32_t packetCount=0;
    uint64_t packetStartMs;

    NSLog(@"reading block at offset %llx", block.fileOffset);
    osp_assert(block.sealed && !block.dirty);
    filep = fopen([fileName cStringUsingEncoding: NSUTF8StringEncoding], "r");
    if (!filep)
	return -1;
    fseek(filep, block.fileOffset, SEEK_SET);

    packetStartMs = block.baseMs;
    while(true) {
	code = fread(&shortTemp, 2, 1, filep);
	if (code != 1) {
	    NSLog(@"read inconsistency after %d packets A", packetCount);
	    fclose(filep);
	    return -1;
	}

	if (shortTemp == _kTrailerMagic) {
	    // block should end with a trailer magic
	    fclose(filep);
	    NSLog(@"read success (no inconsistency) with packetcount=%d B", packetCount);
	    return 0;
	}

	if (shortTemp != _kMagic) {
	    NSLog(@"read inconsistency after %d packets C", packetCount);
	    fclose(filep);
	    return -2;
	}

	// packet data count
	code = fread(&shortTemp, 2, 1, filep);
	if (code != 1) {
	    NSLog(@"read inconsistency after %d packets D", packetCount);
	    fclose(filep);
	    return -1;
	}

	if (shortTemp > _kBytesPerBlock) {
	    NSLog(@"read inconsistency after %d packets E", packetCount);
	    fclose(filep);
	    return -2;
	}

	MFANAqStreamPacket *packet = [[MFANAqStreamPacket alloc] init];

	tdatap = (char *) malloc(shortTemp);
	code = fread(tdatap, shortTemp, 1, filep);
	if (code != 1) {
	    NSLog(@"read inconsistency after %d packets F", packetCount);
	    free(tdatap);
	    fclose(filep);
	    return -1;
	}

	[packet setData: std::string(tdatap, shortTemp)];
	free(tdatap);

	code = fread(&longTemp, 4, 1, filep);
	if (code != 1) {
	    NSLog(@"read inconsistency after %d packets K", packetCount);
	    fclose(filep);
	    return -1;
	}

	// we know where the block started (and keep a rolling update
	// of the packet start times in packetStartMs).  Update the
	// next packet start time based on this packet's duration.
	packet.startMs = packetStartMs;
	packet.durationMs = longTemp;
	packetStartMs += longTemp;

	code = fread(&shortTemp, 2, 1, filep);
	if (code != 1) {
	    NSLog(@"read inconsistency after %d packets G", packetCount);
	    fclose(filep);
	    return -1;
	}

	code = fread([packet getDescrAddr], sizeof(packet.descr), 1, filep);
	if (code != 1) {
	    NSLog(@"read inconsistency after %d packets H", packetCount);
	    return -2;
	}

	// Now read the playing song
	code = fread(&shortTemp, 2, 1, filep);
	if (code != 1) {
	    NSLog(@"read inconsistency after %d packets I", packetCount);
	    fclose(filep);
	    return -1;
	}

	if (shortTemp > 0) {
	    tdatap = (char *) malloc(shortTemp+1);	// extra for added null termination
	    code = fread(tdatap, shortTemp, 1, filep);
	    if (code != 1) {
		NSLog(@"read inconsistency after %d packets J", packetCount);
		free(tdatap);
		fclose(filep);
		return -1;
	    }
	    tdatap[shortTemp] = 0;	// null terminate
	    packet.playingSong = [NSString stringWithUTF8String: tdatap];
	    free(tdatap);
	} else {
	    packet.playingSong = @"";
	}

	// we have a complete packet, now append it; timestamps are
	// already present.
	[block.packetArray addObject: packet];
	packetCount++;
    } // loop over all packets
}

- (int32_t) writePacketsToBlock: (MFANAqStreamBlock *) block {
    NSString *fileName = fileNameForFileId(_streamFile->_fileId);
    FILE *filep;
    size_t code;
    uint16_t shortTemp;
    uint32_t longTemp;

    NSLog(@"writing block at offset %llx", block.fileOffset);
    // opens an existing file for read and write without truncating it
    filep = fopen([fileName cStringUsingEncoding: NSUTF8StringEncoding], "r+");
    if (!filep) {
	NSLog(@"write failure!");
	return -1;
    }
    fseek(filep, block.fileOffset, SEEK_SET);

    MFANAqStreamPacket *packet;
    for(packet in block.packetArray) {
	shortTemp = _kMagic;
	code = fwrite(&shortTemp, 2, 1, filep);
	if (code != 1) {	// returns # of 2 byte items, not # of bytes
	    NSLog(@"write failure 1!");
	    fclose(filep);
	    return -1;
	}

	shortTemp = [packet getLength];
	code = fwrite(&shortTemp, 2, 1, filep);
	if (code != 1) {
	    NSLog(@"write failure 2!");
	    fclose(filep);
	    return -1;
	}

	code = fwrite([packet getData], shortTemp, 1, filep);
	if (code != 1) {
	    NSLog(@"write failure 3!");
	    fclose(filep);
	    return -1;
	}

	longTemp = (uint32_t) packet.durationMs;
	code = fwrite(&longTemp, 4, 1, filep);
	if (code != 1) {
	    NSLog(@"write failure 4");
	    fclose(filep);
	    return -1;
	}

	shortTemp = sizeof(AudioStreamPacketDescription);
	code = fwrite(&shortTemp, 2, 1, filep);
	if (code != 1) {
	    NSLog(@"write failure 5!");
	    fclose(filep);
	    return -1;
	}

	code = fwrite([packet getDescrAddr], shortTemp, 1, filep);
	if (code != 1) {
	    NSLog(@"write failure 6!");
	    fclose(filep);
	    return -1;
	}

	shortTemp = (uint16_t) [packet.playingSong length];
	code = fwrite(&shortTemp, 2, 1, filep);
	if (code != 1) {
	    NSLog(@"write failure 7!");
	    fclose(filep);
	    return -1;
	}

	if (shortTemp > 0) {
	    code = fwrite([packet.playingSong cStringUsingEncoding: NSUTF8StringEncoding],
			  shortTemp, 1, filep);
	    if (code != 1) {
		NSLog(@"write failure 8!");
		fclose(filep);
		return -1;
	    }
	}
    } // loop over all records in this block

    // write end
    shortTemp = _kTrailerMagic;
    code = fwrite(&shortTemp, 2, 1, filep);
    if (code != 1) {
	NSLog(@"write failure 9!");
	fclose(filep);
	return -1;
    }

    code = fclose(filep);
    if(code != 0) {
	NSLog(@"write failure -- close!");
    }
    return 0;
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

- (MFANAqStreamBuffer *) initWithFileId: (uint32_t) fileId {
    static uint32_t fileIdGenerator = 1;
    self = [super init];
    if (self != nil) {
	MFANAqStreamBlock *block;

        if (!_bufferStaticSetup) {
            _bufferStaticSetup = YES;
            pthread_mutex_init(&_bufferMutex, NULL);
        }
        pthread_cond_init(&_packetArrayCv, NULL);
	pthread_cond_init(&_pthreadReqCv, NULL);
	pthread_cond_init(&_pthreadDoneCv, NULL);
	pthread_cond_init(&_blockIoCv, NULL);

        _packetArray = [[NSMutableOrderedSet alloc] init];
        _shuttingDown = NO;
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
	_fileSize = _kBytesPerBlock;

	_streamFile = new MFANAqStreamFile();
	_streamFile->_blocks = [[NSMutableArray alloc] init];
	_streamFile->_lru = [[NSMutableOrderedSet alloc] init];
	if (fileId == 0)
	    _streamFile->_fileId = fileIdGenerator++;
	else
	    _streamFile->_fileId = fileId;

	// and create the backing file
	int fd = open([fileNameForFileId(_streamFile->_fileId)
			  cStringUsingEncoding: NSUTF8StringEncoding],
		      O_CREAT | O_WRONLY | O_TRUNC, 0666);
	osp_assert(fd >= 0);
	close(fd);

	// create first block so we have somewhere to put data.
	// Invariant is that this block always exists, but isn't in
	// the LRU and isn't marked as dirty until it is complete.
	block = [[MFANAqStreamBlock alloc] initWithBuffer: self];

        _streamBufferThread = [[NSThread alloc] initWithTarget: self
                                                      selector: @selector(ioAsync:)
                                                        object: nil];
        [_streamBufferThread start];
    }
    return self;
}

- (void) dealloc {
    // cleanup c++ allocated structures
    if (_streamFile) {
	// ARC doesn't know to walk into streamFile to find references
	// to release.
	_streamFile->_blocks = nil;
	_streamFile->_lru = nil;
	delete _streamFile;
	_streamFile = nullptr;
    }
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
    [self readPacketsFromBlock: block];

    NSLog(@"fill done baseMs=%lld o=%llx", block.baseMs, block.fileOffset);
    block.ioRunning = false;
    osp_assert(!block.valid);
    block.valid = true;
    _validBlocks++;
    [self fixLru: block];
    pthread_cond_broadcast(&_blockIoCv);
}

- (void) cleanBlock: (MFANAqStreamBlock *) block {
    while(block.ioRunning) {
	pthread_cond_wait(&_blockIoCv, &_bufferMutex);
    }
    if (!block.dirty)
	return;

    // once we set ioRunning, no one else should turn off _dirty
    block.ioRunning = true;
    [self writePacketsToBlock: block];

    // allow new IOs to start
    block.ioRunning = false;
    block.dirty = false;
    osp_assert(_dirtyBlocks > 0);
    _dirtyBlocks--;
    pthread_cond_broadcast(&_blockIoCv);
}

- (void) gcAsync: (id) junk {
    int32_t code;
    MFANAqStreamBlock *block;

    NSLog(@"===== GC starts");
    pthread_mutex_lock(&_bufferMutex);

    _gcRunning = true;
    int newFd = open([altFileNameForFileId(_streamFile->_fileId)
			 cStringUsingEncoding: NSUTF8StringEncoding],
		     O_CREAT | O_WRONLY | O_TRUNC, 0666);
    osp_assert(newFd >= 0);
    close(newFd);

    NSString *oldFileName = fileNameForFileId (_streamFile->_fileId);
    FILE *oldFilep = fopen([oldFileName cStringUsingEncoding: NSUTF8StringEncoding], "r");
    NSString *newFileName = altFileNameForFileId (_streamFile->_fileId);
    FILE *newFilep = fopen([newFileName cStringUsingEncoding: NSUTF8StringEncoding], "r+");


    // the first block will appear at offset 0 of the new file, so its
    // offset is the shift.
    uint64_t gcByteShift = _streamFile->_blocks[0].fileOffset;
    _streamFile->_gcBlockShift = (uint32_t) gcByteShift / _kBytesPerBlock;

    NSLog(@"====starting GC block count=%ld removing %d blocks",
	  (long) [_streamFile->_blocks count], _streamFile->_gcBlockShift);

    char *diskBufferp = (char *) malloc(_kBytesPerBlock);
    BOOL failed = false;

    // The concurrency issues are a little tricky, since _blocks can
    // be growing at the end during this time.  But there can be no
    // blocks added from the time blockIx reaches the end and the time
    // we finally swap the new file for the old.  So, the whole thing
    // looks like an atomic change.
    //
    // Note that the file offset changes, but the ms label doesn't.
    for(uint32_t blockIx = 0; blockIx < [_streamFile->_blocks count]; blockIx++) {
	block = _streamFile->_blocks[blockIx];
	// the last block may not be sealed (typically isn't)
	if (!block.sealed) {
	    osp_assert(blockIx == [_streamFile->_blocks count] - 1);
	    break;
	}

	while (block.ioRunning) {
	    pthread_cond_wait(&_blockIoCv, &_bufferMutex);
	}
	block.ioRunning = true;

	// copy both valid and non-valid blocks, since valid just
	// means that the memory data is not valid.
	pthread_mutex_unlock(&_bufferMutex);

	// now copy the block
	fseek(oldFilep, block.fileOffset, SEEK_SET);
	code = (int32_t) fread(diskBufferp, 1, _kBytesPerBlock, oldFilep);
	if (code < _kBytesPerBlock) {
	    // Note that we can get short reads on the last sealed
	    // block, but since this is all unlocked, it is hard to
	    // tell here if the block we tried to read was the last
	    // block.
	    if (ferror(oldFilep)) {
		// may get short read on last sealed block
		NSLog(@"====GC read failure at offset %llx", block.fileOffset);
		failed = true;
		block.ioRunning = false;
		break;
	    }
	}
	fseek(newFilep, block.fileOffset - gcByteShift, SEEK_SET);
	code = (int32_t) fwrite(diskBufferp, 1, _kBytesPerBlock, newFilep);
	if (code != _kBytesPerBlock) {
	    NSLog(@"====GC write failure");
	    block.ioRunning = false;
	    failed = true;
	    break;
	}

	// and relock now that the work is done.
	pthread_mutex_lock(&_bufferMutex);

	block.ioRunning = false;
	pthread_cond_broadcast(&_blockIoCv);
    }

    // in case we took an error path
    pthread_cond_broadcast(&_blockIoCv);

    free(diskBufferp);

    // At this point, all we have left to do is a rename and update
    // the offsets

    // we're done with the open files.
    fflush(oldFilep);
    fsync(fileno(oldFilep));
    fclose(oldFilep);

    fflush(newFilep);
    fsync(fileno(newFilep));
    fclose(newFilep);

    _gcRunning = false;

    code = rename([newFileName cStringUsingEncoding: NSUTF8StringEncoding],
		  [oldFileName cStringUsingEncoding: NSUTF8StringEncoding]);
    osp_assert(code == 0);

    NSLog(@"====GC done");

    for(block in _streamFile->_blocks) {
	// offsets in GC file are correct for offsets in regular file after
	// rename of GC file to be the main file.
	osp_assert(block.fileOffset >= gcByteShift);
	block.fileOffset -= gcByteShift;
    }

    pthread_mutex_unlock(&_bufferMutex);
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
		    block = _streamFile->_lru[0];
		    if (block.dirty) {
			[self cleanBlock: block];
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
		uint32_t blockCount = (uint32_t) [_streamFile->_blocks count];
		while(_dirtyBlocks > 1) {
		    bool foundAny = false;
		    for(int32_t i=blockCount - 2; i >= 0; i--) {
			block = _streamFile->_blocks[i];
			if (block.dirty) {
			    [self cleanBlock: block];
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

// prune records so that all records older than pruneLength ms before
// the last appended packet get deleted.
- (void) pruneOldestMs: (uint64_t) pruneLength {
    uint64_t startMs;
    MFANAqStreamBlock *block;
    uint32_t blockCount;

    pthread_mutex_lock(&_bufferMutex);
    if (pruneLength > _lastPacketEndMs) {
	pthread_mutex_unlock(&_bufferMutex);
        return;
    } else
        startMs = _lastPacketEndMs - pruneLength;

    // Remove whole blocks, since each block is perhaps 0.5 - 2.0 seconds, and
    // that's good enough.
    while (true) {
	// be careful never to remove the last block, since the code
	// in this module assumes there's always at least one block in
	// the array.
	blockCount = (uint32_t) [_streamFile->_blocks count];
	if (blockCount > 1) {
	    block = _streamFile->_blocks[0];
	    while(block.ioRunning) {
		pthread_cond_wait(&_blockIoCv, &_bufferMutex);
	    }
	    if (block.baseMs < startMs) {
		NSLog(@"prune removing block %lld/%lld o=%llx bix=0",
		      block.baseMs, block.durationMs, block.fileOffset);
		// this will free all the data in memory, but the disk
		// file will still need to be compacted eventually.
		[_streamFile->_blocks removeObjectAtIndex: 0];

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

    blockCount = (uint32_t) [_streamFile->_blocks count];
    if (blockCount <= 0)
	_firstPacketStartMs = 0;
    else {
	block = _streamFile->_blocks[0];
	_firstPacketStartMs = block.baseMs;
    }

    // Now check to see if we need to start a GC.  We start one if the file is more
    // than 50% garbage.  We can tell by seeing how many blocks are in the file,
    // and where the first block's file offset is.
    if ( !_gcRunning && !_shuttingDown &&
	 (100 * (_streamFile->_blocks[0].fileOffset / _kBytesPerBlock) / blockCount >
	  _kMaxDiskPct)) {
	_gcRunning = true;
        _gcBufferThread = [[NSThread alloc] initWithTarget: self
						  selector: @selector(gcAsync:)
						    object: nil];
	[_gcBufferThread start];
    }
    pthread_mutex_unlock(&_bufferMutex);
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
    prevBlock = [_streamFile->_blocks lastObject];

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

    newBlock.baseMs = prevBlock.durationMs + prevBlock.baseMs;
    newBlock.fileOffset = prevBlock.fileOffset + _kBytesPerBlock;
    // caller will add newBLock to array at count offset
    NSLog(@"addBlockAndSeal sealed block startMs=%lld %lld packets newBlock startMs=%lld bix=%llu",
	  prevBlock.baseMs, (uint64_t) [prevBlock.packetArray count],
	  newBlock.baseMs, (uint64_t) [_streamFile->_blocks count]);
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
	[_streamFile->_lru addObject: block];
	block.inLru = true;
    } else if (!needLru && block.inLru) {
	[_streamFile->_lru removeObject: block];
	block.inLru = false;
    }
}

- (void) addPacket: (MFANAqStreamPacket *) packet withDuration: (uint32_t) durationMs {
    MFANAqStreamBlock *block;
    uint32_t packetBytesUsed;

    packet.startMs = _lastPacketEndMs;
    packet.durationMs = durationMs;
    packetBytesUsed = [self packetDiskSize: packet];

    pthread_mutex_lock(&_bufferMutex);
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

@end
