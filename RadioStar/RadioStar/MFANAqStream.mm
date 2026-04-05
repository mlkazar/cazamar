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

#include <string>

#include <stdio.h>
#include <pthread.h>
#include "bufsocket.h"
#include "radiostream.h"

#define _showIo false

static int _streamStaticSetup = 0;
static pthread_mutex_t _streamMutex;

// ---------------------------------------------------------------------------
// MFANAqStream — downloader
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

    BOOL _pthreadWaiters;
    NSThread *_radioStreamThread;

    uint32_t _streamAttachCounter;

    uint64_t _lastDataBytes;
    uint64_t _lastDataMs;

    NSString *_urlString;

    pthread_mutex_t _streamMutex;

    id _failureCallbackObj;
    SEL _failureCallbackSel;

    bool _failed;
    uint32_t _failedWindowMs;       // window size for hard-failure detection
    uint32_t _failedWindowCount;    // max failures within the window

    // The buffer that accumulates decoded packets.
    MFANAqStreamBuffer *_buffer;
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

        aqp->_buffer.dataFormat    = fmt;
        aqp->_buffer.haveProperties = YES;

        float frameDuration  = 1.0f / fmt.mSampleRate;
        float packetDuration = fmt.mFramesPerPacket / fmt.mSampleRate;

        aqp->_buffer.frameDuration  = frameDuration;
        aqp->_buffer.packetDuration = packetDuration;

        NSLog(@"frame duration=%f packetDuration=%f",
              frameDuration, packetDuration);
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

    packetsCopied = 0;
    bytesCopied = 0;
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

        if (framesInPacket > 0)
            NSLog(@"packet framesInPacket=%lld duration=%f generic packet duration=%f",
                  framesInPacket, framesInPacket * aqp->_buffer.frameDuration, aqp->_buffer.packetDuration);

	[aqp->_buffer addPacket: packet withDuration: durationMs];

        packetsCopied++;
        bytesCopied += packetSize;
    }

    // NB: keep this well above the implicit delay from the stream player's
    // audio queue (~32 seconds at 64 Kbps) to avoid pruning data before the
    // player has had a chance to read it for the first time.
    [aqp->_buffer pruneOldestMs: 1800000];  // 30 minutes in ms
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

- (MFANAqStream *) initWithUrl: (NSString *) url buffer:(MFANAqStreamBuffer *) buffer {
    self = [super init];
    if (self) {
        NSLog(@"- AqStream init starts for %p", self);

        if (!_streamStaticSetup) {
            _streamStaticSetup = YES;
            pthread_mutex_init(&_streamMutex, NULL);
        }

        // Create the buffer first; it initialises the shared mutex.
        _buffer = buffer;

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

        _failed = NO;
        _failedWindowMs = 4000;
        _failedWindowCount = 6;

        _radioStreamThread = [[NSThread alloc] initWithTarget: self
                                                      selector: @selector(playAsync:)
                                                        object: nil];
        [_radioStreamThread start];
    }
    return self;
}

/* The async thread that drives the RadioStream download. */
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

    if (!_shuttingDown && _failureCallbackObj != nil) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_failureCallbackObj performSelector: self->_failureCallbackSel
                                            withObject: self];
        });
    }
#pragma clang diagnostic pop

    pthread_cond_broadcast(&_pthreadIdleCv);

    threadReference = nil;
    pthread_exit(NULL);
}

- (void) shutdown {
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
            [self shutdown];
        });
        return;
    }

    _shuttingDown = YES;

    // abort any readers, so that the streamplayer can be shutdown and
    // deleted.
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
