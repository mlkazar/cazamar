#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#import "MFANAqStreamBuffer.h"

#include <string>
#include <pthread.h>

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

- (int32_t) addData: (char *) data descr: (AudioStreamPacketDescription *) descr {
    _data.append(data, descr->mDataByteSize);
    _descr = *descr;
    return 0;
}

// *not* null terminated
- (char *) getData {
    return (char *) _data.data();
}

- (uint64_t) getLength {
    return _data.length();
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
// ---------------------------------------------------------------------------

@implementation MFANAqStreamReader {
    MFANAqStreamBuffer *_streamBuffer;
    uint64_t _packetStreamVersion;  // detects when cached index is stale
    uint64_t _recordMs;             // current position in the stream (ms)
    uint64_t _ix;                   // index of the next packet to read
    bool _closed;
}

- (MFANAqStreamReader *) initWithBuffer: (MFANAqStreamBuffer *) buffer {
    self = [super init];
    if (self != nil) {
        _streamBuffer = buffer;
	pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);
        MFANAqStreamPacket *packet;
        uint64_t recordCount = [buffer.packetArray count];

        NSLog(@"AqStream attaching reader after %lld stream packets", recordCount);

        // start past the last record already in the stream
	packet = [buffer.packetArray lastObject];
	if (packet != nil)
	    _recordMs = packet.startMs + packet.durationMs;
	else
	    _recordMs = 0;

        _ix = recordCount;
        _packetStreamVersion = buffer.packetStreamVersion;
        _closed = NO;

	pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
    }

    return self;
}

// Called with bufferMutex held.
- (bool) indexIsValid {
    return (_packetStreamVersion == _streamBuffer.packetStreamVersion);
}

// Called with bufferMutex held.
// Returns the index of the first packet with startMs >= ms.
- (uint64_t) findPacketIx: (uint64_t) ms {
    uint64_t ix;
    MFANAqStreamPacket *packet;

    for(ix = 0; ix < [_streamBuffer.packetArray count]; ix++) {
        packet = [_streamBuffer.packetArray objectAtIndex: ix];
        if (packet.startMs >= ms)
            break;
    }

    NSLog(@"findPacketIx for %lld ms returns %lld", ms, ix);
    return ix;
}

- (uint64_t) seek: (uint64_t) ms whence: (int) how {
    pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);
    _ix = [self findPacketIx: ms];
    _recordMs = ms;
    NSLog(@"==>seek to ms=%lld index=%lld", ms, _ix);
    pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);

    // wake up any pending read
    pthread_cond_broadcast([_streamBuffer packetArrayCv]);
    return _ix;
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
// the downloader finishes.  Returns YES if the target was met.
- (bool) waitForAtLeast: (uint64_t) targetBytes {
    bool rval;
    pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);

    rval = false;
    while(true) {
        if (_streamBuffer.shuttingDown || _closed) {
            rval = false;
            break;
        }

        if (!self.indexIsValid) {
            _ix = [self findPacketIx: _recordMs];
            _packetStreamVersion = _streamBuffer.packetStreamVersion;
        }

        uint64_t totalPackets = [_streamBuffer.packetArray count] - _ix;
        uint64_t totalBytes = 0;
        if (totalPackets > 0) {
            totalBytes = [_streamBuffer.packetArray[_ix] getLength] * totalPackets;
        } else {
            totalBytes = 0;
        }
        if (totalBytes >= targetBytes) {
            rval = true;
            break;
        }

        pthread_cond_wait([_streamBuffer packetArrayCv], [MFANAqStreamBuffer bufferMutex]);
        continue;
    }

    pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
    return rval;
}

- (bool) hasData {
    bool rval;

    pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);

    if (!self.indexIsValid) {
        _ix = [self findPacketIx: _recordMs];
        _packetStreamVersion = _streamBuffer.packetStreamVersion;
    }

    rval = (_ix < [_streamBuffer.packetArray count]);

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

    pthread_mutex_lock([MFANAqStreamBuffer bufferMutex]);
    while(true) {
        // Abort if the buffer itself is being torn down or the reader was closed.
        if (_streamBuffer.shuttingDown || _closed) {
            pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
            return nil;
        }

        if (!self.indexIsValid) {
            _ix = [self findPacketIx: _recordMs];
            _packetStreamVersion = _streamBuffer.packetStreamVersion;
        }

        if (_ix >= [_streamBuffer.packetArray count]) {
            // No packet at the current index.
            pthread_cond_wait([_streamBuffer packetArrayCv], [MFANAqStreamBuffer bufferMutex]);
            continue;
        }

        packet = _streamBuffer.packetArray[_ix];
        _recordMs = packet.startMs + packet.durationMs;
        _ix++;
        break;
    }
    pthread_mutex_unlock([MFANAqStreamBuffer bufferMutex]);
    packet.read = YES;
    return packet;
}

@end

// ---------------------------------------------------------------------------
// MFANAqStreamBuffer
// ---------------------------------------------------------------------------

static int _bufferStaticSetup = 0;
static pthread_mutex_t _bufferMutex;

@implementation MFANAqStreamBuffer {
    pthread_cond_t _packetArrayCv;
}

+ (pthread_mutex_t *) bufferMutex {
    return &_bufferMutex;
}

- (pthread_cond_t *) packetArrayCv {
    return &_packetArrayCv;
}

- (uint32_t) packetCount {
    uint32_t count;
    pthread_mutex_lock(&_bufferMutex);
    count = [_packetArray count];
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

- (MFANAqStreamBuffer *) init {
    self = [super init];
    if (self != nil) {
        if (!_bufferStaticSetup) {
            _bufferStaticSetup = YES;
            pthread_mutex_init(&_bufferMutex, NULL);
        }
        pthread_cond_init(&_packetArrayCv, NULL);

        _packetArray = [[NSMutableOrderedSet alloc] init];
        _packetStreamVersion = 1;
        _shuttingDown = NO;
        _haveProperties = NO;
        _lastPacketEndMs = 0;
        _packetDuration = 0.0;
        _frameDuration = 0.0;
    }
    return self;
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

    pthread_mutex_lock(&_bufferMutex);
    if (pruneLength > _lastPacketEndMs) {
	pthread_mutex_unlock(&_bufferMutex);
        return;
    } else
        startMs = _lastPacketEndMs - pruneLength;

    while(true) {
        MFANAqStreamPacket *packet = [_packetArray firstObject];
        if (packet == nil)
            break;
        if (packet.startMs >= startMs)
            break;

        // packet exists before the prune time, remove it.
        NSLog(@"pruning packet with startMs=%lld (startMs=%lld)", packet.startMs, startMs);
        [_packetArray removeObject: packet];
    }

    // readers' cached indices are now wrong
    _packetStreamVersion++;

    pthread_mutex_unlock(&_bufferMutex);
}

- (void) addPacket: (MFANAqStreamPacket *) packet withDuration: (uint32_t) durationMs {
    packet.startMs = _lastPacketEndMs;
    packet.durationMs = durationMs;

    pthread_mutex_lock(&_bufferMutex);
    _lastPacketEndMs = packet.startMs + durationMs;
    [_packetArray addObject: packet];
    pthread_mutex_unlock(&_bufferMutex);

    // wakeup anyone waiting
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

- (void) getDataFormat: (AudioStreamBasicDescription *) format {
    *format = _dataFormat;
}

@end
