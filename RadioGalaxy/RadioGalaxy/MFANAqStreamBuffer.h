#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>

// ---------------------------------------------------------------------------
// MFANAqStreamPacket
//
// One decoded audio packet received from the radio stream, together with the
// metadata needed to schedule and seek it.
// ---------------------------------------------------------------------------

@class MFANAqStreamBlock;

typedef struct _MFANAqStreamFile {
    NSMutableArray<MFANAqStreamBlock *> *_blocks;
    NSMutableOrderedSet<MFANAqStreamBlock *> *_lru;
    uint32_t _fileId;
    uint32_t _gcBlockShift;
} MFANAqStreamFile;

@interface MFANAqStreamPacket : NSObject

@property NSString *playingSong;
@property uint64_t startMs;
@property uint32_t durationMs;
@property bool read;
@property AudioStreamPacketDescription descr;

- (int32_t) addData: (char *) data descr: (AudioStreamPacketDescription *) descr;

- (AudioStreamPacketDescription *) getDescrAddr;

- (char *) getData;

- (void) getDescr: (AudioStreamPacketDescription *) descr;

- (uint32_t) getLength;

- (MFANAqStreamPacket *) init;
@end

// ---------------------------------------------------------------------------
// MFANAqStreamReader
//
// A positioned read cursor into an MFANAqStreamBuffer.  Multiple readers may
// exist concurrently over the same buffer.
// ---------------------------------------------------------------------------

@class MFANAqStreamBuffer;

@interface MFANAqStreamReader : NSObject
@property MFANAqStreamBuffer *streamBuffer;
@property uint32_t packetStreamVersion;
@property uint64_t recordMs;
@property AudioStreamPacketDescription descr;

- (MFANAqStreamPacket *) read;

- (bool) hasData;

- (bool) waitForAtLeast: (uint64_t) targetBytes;

// Returns the absolute seek position (packet index).
// whence is currently unused; pass 0 for an absolute-ms seek.
- (void) seek:(uint64_t) ms whence: (int) whence;

- (uint64_t) tell;

// Create a new reader positioned just past the last packet currently
// in the buffer (i.e. it will read only newly streamed packets).
- (MFANAqStreamReader *) initWithBuffer: (MFANAqStreamBuffer *) buffer;

- (void) close;
@end

@interface MFANAqStreamBlock : NSObject
@property uint64_t baseMs;
@property NSMutableArray<MFANAqStreamPacket *> *packetArray;
@property uint64_t durationMs;
@property uint64_t fileOffset;
@property uint32_t diskBytesUsed;
@property uint8_t pinCount;
@property BOOL valid;
@property BOOL dirty;
@property BOOL ioRunning;
@property BOOL sealed;
@property BOOL inLru;

- (BOOL) validContents;

@end

// ---------------------------------------------------------------------------
// MFANAqStreamBuffer
//
// Stores the sequence of streamed audio packets (MFANAqStreamPacket).
// Its lifetime is independent from MFANAqStream (the streamer): callers
// may retain the buffer and continue reading already-streamed data after
// the streamer has been released or shut down.
//
// Thread-safety: all mutations and reads of packetArray and the version/
// timestamp fields must be done with +streamMutex held.  The condition
// variable returned by -packetArrayCv is used to signal waiting readers.
// ---------------------------------------------------------------------------

@interface MFANAqStreamBuffer : NSObject

// The ordered set of MFANAqStreamPacket objects, oldest first.
@property NSMutableOrderedSet *packetArray;

// Incremented whenever a packet is removed so that readers can detect that
// their cached index into packetArray has been invalidated.
@property uint32_t packetStreamVersion;

// Set to YES when the buffer itself is being torn down.  Any pending reads
// or waits will abort immediately and return nil / false.
@property BOOL shuttingDown;

// Seconds per audio packet, derived from the stream's data format.
@property float packetDuration;

// pointer to file descriptor.
@property MFANAqStreamFile *streamFile;

// the end timestamp of last packet in the last block
@property uint64_t lastPacketEndMs;

// the start timestamp of first packet in the first block
@property uint64_t firstPacketStartMs;

@property uint64_t fileSize;

@property uint32_t validBlocks;

@property uint32_t dirtyBlocks;

// -------------------------------------------------------------------
// Fields written by the streamer's audio-stream parser callbacks.
// Exposed as properties so the static C callbacks in MFANAqStream.mm
// can access them without needing to reach into private ivars.
// -------------------------------------------------------------------
@property BOOL haveProperties;
@property float frameDuration;
@property AudioStreamBasicDescription dataFormat;

// -------------------------------------------------------------------
// Synchronisation primitives (shared across all buffer instances).
// -------------------------------------------------------------------

// add a packet, updating global time information in buffer.
- (void) addPacket: (MFANAqStreamPacket *) packet withDuration: (uint32_t) durationMs;

// Global mutex that guards packetArray and the condition variable.  A
// single mutex is shared so that a mutex is not required in every
// reader in order to access the stream.  Actually, Claude code made
// up that reason -- the real reason is that if we have a callback
// upcalled from multiple threads, it's nice to be able to get a lock
// on an object that might have been deleted.
+ (pthread_mutex_t *) bufferMutex;

// Per-instance condition variable.  Signalled when:
//   • new packets are appended,
//   • shuttingDown changes to YES.
- (pthread_cond_t *) packetArrayCv;

// -------------------------------------------------------------------
// Audio format helpers (delegate to the stored dataFormat).
// -------------------------------------------------------------------
- (NSString *) getDataFormatString;
- (void) getDataFormat: (AudioStreamBasicDescription *) format;

// -------------------------------------------------------------------
// Packet management
// -------------------------------------------------------------------

// cleanup files for a particular file ID
+ (void) cleanupFileId: (uint32_t) fileId;

// after stopping readers, reenable reads
- (void) allowReaders;

// abort alll readers.  No new reads will succeed until allowReaders
// is called.
- (void) abortReaders;

// Must be called with +bufferMutex held.
// Removes all packets whose startMs is earlier than
//   (lastPacketEndMs - pruneLength).
- (void) pruneOldestMs: (uint64_t) pruneLength;

- (uint32_t) packetCount;

- (MFANAqStreamPacket *) lastPacket;

- (MFANAqStreamBuffer *) initWithFileId: (uint32_t) fileId;

- (void) shutdown;

- (int32_t) readPacketsFromBlock: (MFANAqStreamBlock *) block;

- (void) fillBlock: (MFANAqStreamBlock *) block;

- (void) cleanBlock: (MFANAqStreamBlock *) block;

- (MFANAqStreamBlock *) lastBlockSetIndex: (uint32_t *) indexp;

- (MFANAqStreamBlock *) findBlockAtMs: (uint64_t) ms
			     setIndex: (uint32_t *) indexp;

- (void) fixLru: (MFANAqStreamBlock *) block;

- (void) unpin: (MFANAqStreamBlock *) block;

- (MFANAqStreamBlock *) pin: (MFANAqStreamBlock *) block;

- (BOOL) blockIx: (uint32_t) blockIx
	packetIx: (uint32_t) packetIx
      containsMs: (uint64_t) ms;
@end
