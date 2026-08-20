#import <MediaPlayer/MediaPlayer.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudioTypes.h>
#import <CoreFoundation/CoreFoundation.h>

#import "MFANAqStream.h"
#import "MFANAqStreamBuffer.h"
#import "MFANCGUtil.h"
#import "MFANSocket.h"
#import "Settings.h"
#import "ViewController.h"

#include <string>

#include <stdio.h>
#include <pthread.h>
#include "bufsocket.h"
#include "radiostream.h"

#define _showIo false

static int _streamStaticSetup = 0;
static pthread_mutex_t _streamMutex;

// ---------------------------------------------------------------------------
// MFANAqStream — streamer
// ---------------------------------------------------------------------------

@implementation MFANAqStream {
    BOOL _shuttingDown;
    BOOL _pthreadDone;
    pthread_cond_t _pthreadIdleCv;

    AudioFileStreamID _audioStreamHandle;
    RadioStream *_radioStreamp;

    // Number of active upcalls from the audio-stream parser.
    uint32_t _activeParseCalls;

    FILE *_recordingFilep;

    // The song title currently being broadcast by the station.
    NSString *_currentPlaying;

    float _dataRate;            /* estimated data rate */

    BOOL _setErrorFlag;		// have we set the error flag on a
				// packet for this stream yet?
    BOOL _setPropagatedErrorFlag;	// we still need to propagate
					// this error to a named
					// packet.

    BOOL _pthreadWaiters;
    NSThread *_radioStreamThread;

    uint32_t _streamAttachCounter;

    uint64_t _lastDataBytes;
    uint64_t _lastDataMs;

    NSString *_urlString;

    pthread_mutex_t _streamMutex;

    id _failureCallbackObj;
    SEL _failureCallbackSel;

    // The buffer that accumulates decoded packets.
    MFANAqStreamBuffer *_buffer;

    ViewController *_vc;

    char _adtsHeader[7];
    uint8_t _adtsCount;
}

- (MFANAqStreamBuffer *) buffer {
    return _buffer;
}

- (float) packetDuration {
    return _buffer.packetDuration;
}

- (void) setFailureCallback: (id) callbackObj sel: (SEL) callbackSel {
    _failureCallbackObj = callbackObj;
    _failureCallbackSel = callbackSel;
}

// ---------------------------------------------------------------------------
// Audio-stream parser callbacks
//
// These are static C functions registered with AudioFileStreamOpen.  They are
// called from within rsDataProc (which holds +streamMutex), so they do not
// need to acquire the mutex themselves.
// ---------------------------------------------------------------------------

/* Called once the parser has determined the stream's encoding parameters. */
void
MFANAqStream_PropertyProc( void *contextp,
                            AudioFileStreamID audioFilep,
                            AudioFileStreamPropertyID propertyId,
                            UInt32 * ioFlags)
{
    MFANAqStream *aqp = (__bridge MFANAqStream *) contextp;
    OSStatus osStatus;
    uint32_t dataFormatSize;

    NSLog(@"in AqStream property proc");

    if (propertyId == kAudioFileStreamProperty_ReadyToProducePackets) {
        AudioStreamBasicDescription fmt;
        dataFormatSize = sizeof(fmt);
        osStatus = AudioFileStreamGetProperty( aqp->_audioStreamHandle,
                                               kAudioFilePropertyDataFormat,
                                               (UInt32 *) &dataFormatSize,
                                               &fmt);

        NSLog(@"PropertyProc has properties");

	// make sure we've received the adtsHeader already.  If so,
	// save a prototype of the adtsHeader.
	osp_assert(aqp->_adtsCount == sizeof(aqp->_adtsHeader));
	[aqp->_buffer
	    setDataFormat: &fmt
	    adtsHeader: aqp->_adtsHeader];
    }
}

/* Called when one or more decoded audio packets are available. */
void
MFANAqStream_PacketsProc( void *contextp,
                           UInt32 numBytes,
                           UInt32 numPackets,
                           const void *inDatap,
                           AudioStreamPacketDescription *packetsp)
{
    MFANAqStream *aqp = (__bridge MFANAqStream *) contextp;
    uint32_t bytesCopied;
    uint32_t packetsCopied;
    uint32_t durationMs;
    uint32_t copiedOffset;

    if (!aqp->_buffer.haveProperties) {
        NSLog(@"! MFANAqStream data received before properties callback");
        return;
    }

    if (numPackets == 0) {
        NSLog(@"- PacketsProc shutting down audioqueue due to no packets");
        return;
    }

    if (_showIo) {
        NSLog(@"AqStream parser received %d bytes w %d packets", numBytes, numPackets);
    }

    // A little tricky here for AAC files.  When saving files from MP3
    // streams, you just concatenate all the packet data (data being
    // from mStartOffset for mDataByteSize) and finally append a
    // trailer giving some identifying metadata.  The packet
    // description information is encoded in the packet data.
    //
    // But for AAC files, that same packet data is preceded by some
    // metadata that provides that packetDescription and that data
    // needs to be written to the file ahead of the actual packet
    // data.
    packetsCopied = 0;
    bytesCopied = 0;
    copiedOffset = 0;
    for(uint32_t i = 0; i < numPackets; i++) {
        int64_t packetOffset        = packetsp[i].mStartOffset;
        int64_t packetSize          = packetsp[i].mDataByteSize;
        int64_t framesInPacket      = packetsp[i].mVariableFramesInPacket;

        MFANAqStreamPacket *packet = [[MFANAqStreamPacket alloc] init];

        if (framesInPacket > 0) {
            durationMs = (uint32_t)(framesInPacket * aqp->_buffer.frameDuration * 1000.0);
        } else {
            durationMs = (uint32_t)(aqp->_buffer.packetDuration * 1000);
        }

        [packet addData: ((char *)inDatap) + packetOffset descr: packetsp+i];
        packet.playingSong = aqp->_currentPlaying;

	if (!aqp->_setErrorFlag) {
	    bool found;
	    aqp->_setErrorFlag = true;	// we've done this test

	    // First packet after an error event, mark the packet as bad if
	    // we can't find the packet in the already recorded data.
	    found = [aqp->_buffer truncateDuplicatesForPacket: packet];
	    if (!found) {
		[packet setErrorCode: 1];
		NSLog(@"setting error on packet @%lld '%@'",
		      packet.startMs, packet.playingSong);

		// if this packet has a song, we don't have to scan
		// for a packet with a song name.  If it doesn't have
		// a song, we *do* have to flag the next packet with a
		// song name.  Goal is to ensure that if we pick up a
		// song in the middle after a conn reset, and the
		// first packet is missing the song name, we still
		// want to make sure the song is marked as bad, since
		// that first packet was probably part of the song,
		// even if the song name is only occasionally
		// transmitted.
		if ([packet.playingSong length] > 0)
		    aqp->_setPropagatedErrorFlag = true;
	    } else {
		// packet is already in truncated block.  We
		// successfully spliced the old and new streams
		// together so We're not setting an error flag at all.
		aqp->_setPropagatedErrorFlag = true;
		continue;
	    }
	}

	// we still have to propagate the error to the first named packet, and
	// we just found a named packet.
	if (!aqp->_setPropagatedErrorFlag && [packet.playingSong length] > 0) {
	    aqp->_setPropagatedErrorFlag = true;
	    [packet setErrorCode: 1];
	}

        if (framesInPacket > 0)
            NSLog(@"packet framesInPacket=%lld duration=%f generic packet duration=%f",
                  framesInPacket, framesInPacket * aqp->_buffer.frameDuration, aqp->_buffer.packetDuration);

	[aqp->_buffer addPacket: packet
		   withDuration: durationMs];

        packetsCopied++;
        bytesCopied += packetSize;
    }

    // NB: keep this well above the implicit delay from the stream player's
    // audio queue (~32 seconds at 64 Kbps) to avoid pruning data before the
    // player has had a chance to read it for the first time.
    Settings *settings = (Settings *) aqp->_vc.settings;

    uint32_t pruneMs = settings.streamBufferMinutes * 60000;

    [aqp->_buffer pruneOldestMs: pruneMs];
}

/* Called by RadioStream with raw (unparsed) data from the HTTP connection. */
/* static */ int32_t
MFANAqStream_rsDataProc(void *contextp, RadioStream *radiop, char *bufferp, int32_t nbytes)
{
    OSStatus osStatus;
    std::string *contentTypep;
    AudioFileTypeID fileType;
    uint64_t now;
    float updateRate;
    uint64_t delta;

    pthread_mutex_lock(&_streamMutex);
    if (radiop->isClosed()) {
        pthread_mutex_unlock(&_streamMutex);
        return -1;
    }

    MFANAqStream *aqp = (__bridge MFANAqStream *) contextp;

    if (aqp->_shuttingDown) {
        pthread_mutex_unlock(&_streamMutex);
        return -1;
    }

    /* update the estimated data rate using exponential decay */
    now = osp_time_ms();
    delta = now - aqp->_lastDataMs;
    if (delta > 2000) {
        updateRate = ((aqp->_lastDataBytes + nbytes) * 1000.0) / delta;
        aqp->_dataRate = aqp->_dataRate * 0.8 + updateRate * 0.2;
        aqp->_lastDataMs = now;
        aqp->_lastDataBytes = 0;
    }
    else {
        aqp->_lastDataBytes += nbytes;
    }

    // accumulate the first 7 bytes of the stream.  If this is an AAC
    // stream, this is a 7 or 9 byte ADTS header, and we'll need to
    // add ADTS headers to each audio packet when writing the file
    // out.  At this point, we don't actually know if this is an AAC file or an
    // MP3, so we save these bytes just in case it turns out to be AAC.
    if (aqp->_adtsCount < sizeof(aqp->_adtsHeader)) {

	uint8_t bytesToCopy = sizeof(aqp->_adtsHeader) - aqp->_adtsCount;
	if (nbytes < bytesToCopy)
	    bytesToCopy = nbytes;

	memcpy(aqp->_adtsHeader + aqp->_adtsCount, bufferp, bytesToCopy);
	aqp->_adtsCount += bytesToCopy;
	if (aqp->_adtsCount == sizeof(aqp->_adtsHeader)) {
	    // turn on 'CRC not present' flag
	    aqp->_adtsHeader[1] |= 1;

	    // zero length bits
	    aqp->_adtsHeader[3] &= 0xC0;// highest 6 bits of length in
					// lowest 6 bits
	    aqp->_adtsHeader[4] = 0;	// next 8 bits of packet
					// length
	    aqp->_adtsHeader[5] &= 0x1F;// least significant 3 bits
					// present in top 3 bits
	}
    }

    /* create the AudioFileStream parser once we know the content type */
    if (aqp->_audioStreamHandle == 0) {
        contentTypep = aqp->_radioStreamp->getContentType();
        fileType = kAudioFileMP3Type;
        if (contentTypep != NULL) {
            if (contentTypep->compare(0,8,"audio/mp") == 0) {
                fileType = kAudioFileMP3Type;
            }
            else if (contentTypep->compare(0,9,"audio/aac") == 0) {
                fileType = kAudioFileAAC_ADTSType;
            }
        }

        osStatus = AudioFileStreamOpen( (__bridge void *) aqp,
                                        MFANAqStream_PropertyProc,
                                        MFANAqStream_PacketsProc,
                                        fileType,
                                        &aqp->_audioStreamHandle);
    }

    if (aqp->_shuttingDown) {
        pthread_mutex_unlock(&_streamMutex);
        return 0;
    }

    aqp->_activeParseCalls++;
    if (nbytes > 0) {
        osStatus = AudioFileStreamParseBytes(aqp->_audioStreamHandle, nbytes, bufferp, 0);
    }
    else {
        NSLog(@"- shutting down audiostream %p due to incoming EOF indicator", aqp);
        if (aqp->_audioStreamHandle) {
            AudioFileStreamClose(aqp->_audioStreamHandle);
            aqp->_audioStreamHandle = NULL;
        }
    }
    aqp->_activeParseCalls--;

    pthread_mutex_unlock(&_streamMutex);
    return 0;
}

/* Called by RadioStream when the currently-playing song title changes. */
int32_t
MFANAqStream_rsControlProc( void *contextp,
                              RadioStream *radiop,
                              RadioStream::EvType event,
                              void *evDatap)
{
    MFANAqStream *aqp;
    NSString *newSong;

    pthread_mutex_lock(&_streamMutex);
    if (radiop->isClosed()) {
        pthread_mutex_unlock(&_streamMutex);
        return -1;
    }

    aqp = (__bridge MFANAqStream *) contextp;

    if (event == RadioStream::eventSongChanged) {
        RadioStream::EvSongChangedData *songp = (RadioStream::EvSongChangedData *) evDatap;
        if (songp->_song.length() > 0) {
            newSong = [NSString stringWithUTF8String: songp->_song.c_str()];
            aqp->_currentPlaying = newSong;
        } else {
            aqp->_currentPlaying = nil;
        }
    }
    else if (event == RadioStream::eventResync) {
        // nothing to do here
    }

    pthread_mutex_unlock(&_streamMutex);
    return 0;
}

// ---------------------------------------------------------------------------
// MFANAqStream — data format helpers (forwarded to the buffer)
// ---------------------------------------------------------------------------

- (NSString *) getDataFormatString {
    return [_buffer getDataFormatString];
}

- (void) getDataFormat: (AudioStreamBasicDescription *) format {
    [_buffer getDataFormat: format];
}

// ---------------------------------------------------------------------------
// MFANAqStream — lifecycle
// ---------------------------------------------------------------------------

- (MFANAqStream *) initWithUrl: (NSString *) url
			buffer:(MFANAqStreamBuffer *) buffer
		      viewCont:(ViewController *) vc {
    self = [super init];
    if (self) {
        NSLog(@"- AqStream init starts for %p", self);

        if (!_streamStaticSetup) {
            _streamStaticSetup = YES;
            pthread_mutex_init(&_streamMutex, NULL);
        }

        // Create the buffer first; it initialises the shared mutex.
        _buffer = buffer;
	_vc = vc;

	_adtsCount = 0;

        _shuttingDown = NO;
        _audioStreamHandle = 0;
        _radioStreamp = nullptr;

        pthread_cond_init(&_pthreadIdleCv, NULL);
        _urlString = url;

        _pthreadDone = NO;
        _recordingFilep = NULL;

        _dataRate = 0.0;
        _lastDataMs = osp_time_ms();
        _lastDataBytes = 0;

        _activeParseCalls = 0;
        _streamAttachCounter = 0;
        _pthreadWaiters = 0;

	// We set _setErrorFlag once we've set the error flag on the
	// first packet after a reconnect (new stream).  It basically
	// means we've considered setting the error flag on the first
	// packet but we don't actually set the error flag if we can
	// resplice the old stream and new together.
	//
	// Similarly, we set setPropagatedErrorFlag once we've considered
	// setting the propagated error flag on the first packet with a
	// _playingSong field set.
	_setErrorFlag = NO;
	_setPropagatedErrorFlag = NO;

	_radioStreamThread = [[NSThread alloc] initWithTarget: self
						     selector: @selector(playAsync:)
						       object: nil];
	[_radioStreamThread start];
    }
    return self;
}

/* The async thread that drives the RadioStream stream. */
- (void) playAsync: (id) junk
{
    MFANSocketFactory socketFactory;
    MFANAqStream *threadReference = self;

    NSLog(@"in playAsync");
    {
        pthread_mutex_lock(&_streamMutex);
        _radioStreamp = new RadioStream();
        pthread_mutex_unlock(&_streamMutex);

        _radioStreamp->init( &socketFactory,
                             (char *) [_urlString cStringUsingEncoding: NSUTF8StringEncoding],
                             MFANAqStream_rsDataProc,
                             MFANAqStream_rsControlProc,
                             (__bridge void *) self);
        NSLog(@"****- aqplayer %p radioplayer done", self);

        pthread_mutex_lock(&_streamMutex);
        if (_radioStreamp != nullptr) {
            _radioStreamp->close();
            _radioStreamp = nullptr;
        }

        if (_audioStreamHandle) {
            NSLog(@"****aqstream closes audiofile");
            AudioFileStreamClose(_audioStreamHandle);
            _audioStreamHandle = nullptr;
        }

        pthread_mutex_unlock(&_streamMutex);
    }

    _pthreadDone = YES;

    NSLog(@"===aqstream shutting down %p failureCallback=%p", self, self->_failureCallbackObj);
    if (!_shuttingDown && _failureCallbackObj != nil) {
	NSLog(@"====about to dispatch to failure callback");
	dispatch_async(dispatch_get_main_queue(), ^{
		[self->_failureCallbackObj performSelectorOnMainThread: self->_failureCallbackSel
							    withObject: self
							 waitUntilDone: true];
	    });
    }

    pthread_cond_broadcast(&_pthreadIdleCv);

    threadReference = nil;
    pthread_exit(NULL);
}

- (void) shutdownAbortReaders: (bool) abortReaders {
    NSLog(@"in MFAqStream shutdown");
    pthread_mutex_lock(&_streamMutex);

    if (_shuttingDown && _pthreadDone) {
        pthread_mutex_unlock(&_streamMutex);
        NSLog(@"- shutdownAudio for stopped player %p", self);
        return;
    }

    if (![NSThread isMainThread]) {
        pthread_mutex_unlock(&_streamMutex);
        NSLog(@" - shutdownAudio bouncing to main thread");
        dispatch_async(dispatch_get_main_queue(), ^{
		[self shutdownAbortReaders: abortReaders];
	    });
        return;
    }

    _shuttingDown = YES;

    // clear these out in case there's an operation that's going to
    // timeout soon.  We don't want to upcall a failure to someone
    // who's going to restart the download.
    _failureCallbackObj = nil;
    _failureCallbackSel = nil;

    // abort any readers, so that the streamplayer can be shutdown and
    // deleted.
    if (abortReaders)
	[_buffer abortReaders];

    if (_radioStreamp != nullptr) {
        _radioStreamp->close();
        _radioStreamp = nullptr;
    }

    while(!_pthreadDone) {
        _pthreadWaiters = YES;
        pthread_cond_wait(&_pthreadIdleCv, &_streamMutex);
    }

    if (_audioStreamHandle != 0) {
        AudioFileStreamClose(_audioStreamHandle);
        _audioStreamHandle = 0;
    }

    pthread_mutex_unlock(&_streamMutex);

    // At this point the pthread has exited.  The buffer (and its packet array)
    // remains alive for as long as any holder retains a reference to it.
}

- (bool) isShutdown {
    return _shuttingDown;
}

- (NSString *) getFinalUrl {
    if (_radioStreamp != nullptr) {
        std::string finalUrl = "http://" + *(_radioStreamp->getStreamUrl());
        return [NSString stringWithUTF8String: finalUrl.c_str()];
    } else {
        return @"[No data]";
    }
}

- (NSString *) getPublicUrl {
    return _urlString;
}

@end
