#import <MediaPlayer/MediaPlayer.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudioTypes.h>
#import <CoreFoundation/CoreFoundation.h>

#import "MFANAqStream.h"
#import "MFANCGUtil.h"
#import "MFANSocket.h"

#include <stdio.h>
#include <pthread.h>
#include "bufsocket.h"
#include "radiostream.h"

@implementation MFANAqStreamPacket {
    uint64_t _ms;
    NSMutableData *_data;
}

- (int32_t) addData: (char *) data length: (uint64_t) length {
    [_data appendBytes: data
		length: length];
    _ms = osp_time_ms();

    return 0;
}

- (char *) getData {
    return (char *) _data.mutableBytes;
}

@end

@implementation MFANAqStream {
    BOOL _shuttingDown;
    BOOL _pthreadDone;
    pthread_cond_t _pthreadIdleCv;

    AudioFileStreamID _audioStreamHandle;
    RadioStream *_radioStreamp;

    // number of active upcalls from the parser
    uint32_t _activeParseCalls;

    BOOL _isRecording;
    BOOL _haveProperties;
    FILE *_recordingFilep;

    NSString *_currentPlaying;

    /* our choices for max # of bytes in a buffer, and packets in a buffer */
    uint32_t _maxBufferSize;
    uint32_t _maxPacketCount;

    /* tracking playing; the stupid AudioQueue property listener
     * doesn't notice if you actually pause playing (another Apple
     * POS).
     */
    float _dataRate;		/* estimated data rate */

    BOOL _pthreadWaiters;
    NSThread *_radioStreamThread;

    MFANAqStreamTarget *_target;

    // counter incremented on each detach
    uint32_t _streamAttachCounter;

    uint64_t _lastDataBytes;
    uint64_t _lastDataMs;

    NSString *_urlString;

    // TODO: get rid of this, or modify getDataFormat to use it
    NSString *_dataFormat;

    // an array of MFANAqStreamPacket objects.
    NSMutableArray *_packetArray;
}

static pthread_mutex_t _streamMutex;
static int _staticSetup = 0;

- (void) shutdown {
    NSLog(@"in shutdown");
    pthread_mutex_lock(&_streamMutex);
    /* check if we've started a shutdown procedure */
    if (_shuttingDown) {
	pthread_mutex_unlock(&_streamMutex);
	NSLog(@"- shutdownAudio for stopped player %p", self);
	return;
    }

    /* if we're called on the wrong thread, move to the right one.  If we don't
     * do this, the NSTimer stuff doesn't work.
     */
    if (![NSThread isMainThread]) {
	pthread_mutex_unlock(&_streamMutex);
	NSLog(@" - shutdownAudio bouncing to main thread");
	dispatch_async(dispatch_get_main_queue(), ^{
		[self shutdown];
	    });
	return;
    }

    _shuttingDown = YES;

    // this is safe to call on any thread, because all it does is set
    // a flag that tells RST to shutdown on the next packet of data.
    if (_radioStreamp != nullptr) {
	_radioStreamp->close();
	_radioStreamp = nullptr;
    }

    // calls to the record parser generate upcalls, and we want to wait for them
    while(!_pthreadDone) {
	_pthreadWaiters = YES;
	pthread_cond_wait(&_pthreadIdleCv, &_streamMutex);
    }

    // once the pthread is done, we can safely close this.
    AudioFileStreamClose(_audioStreamHandle);
    _audioStreamHandle = 0;

    pthread_mutex_unlock(&_streamMutex);

    // At this point, with the reference from pthread and any pthread
    // upcalls all removed, the only ARC references should be from
    // whoever created the AqStream, and it can safely be freed
    // whenever they're done with it.
}

/* this function is called by the AudioFileStreamParsePackets
 * function, once enough data has been received that we can parse the
 * packet info.  Since the radiostream data proc grabs the
 * streamMutex, we don't have to do it here, since our caller is
 * holding it for us.
 */
void
MFANAqStream_PropertyProc( void *contextp,
			   AudioFileStreamID audioFilep,
			   AudioFileStreamPropertyID propertyId,
			   UInt32 * ioFlags)
{
    MFANAqStream *aqp = (__bridge MFANAqStream *) contextp;
    OSStatus osStatus;
    uint32_t dataFormatSize;

    /* this callback is made as soon as the parser has figured out the encoding
     * parameters of the stream.  Until this time, it is really too early to create
     * a lot of the player data structures, since they depend upon the type of
     * stream we're receiving.
     */
    if (propertyId == kAudioFileStreamProperty_ReadyToProducePackets) {
	dataFormatSize = sizeof(aqp->_dataFormat);
	osStatus = AudioFileStreamGetProperty ( aqp->_audioStreamHandle,
						kAudioFilePropertyDataFormat,
						(UInt32 *) &dataFormatSize,
						&aqp->_dataFormat);

	aqp->_haveProperties = YES;
    }
}

/* this function is called by the parser when a non-zero set of
 * records is available.  We upcall it, and save it to
 * any open file.
 *
 * This is called back from the rsDataProc, so it is rsDataProc that
 * gets the streamMutex
 *
 * NB: if we need to do upcalls after releasing the stream mutex, we
 * should do a retain / release on the NSObject under the lock and
 * around the upcall, so that the object can't be freed during the
 * upcall.
 */
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

    if (!aqp->_haveProperties) {
	NSLog(@"! MFANAqStream data received before properties callback");
	return;
    }

    if (numPackets == 0) {
	NSLog(@"- PacketsProc shutting down audioqueue due to no packets");
	if (aqp->_target) {
	    [aqp->_target deliverPacket: NULL length: 0 stream:aqp];
	}
	return;
    }

#if 0
    if (aqp->_isRecording) {
	aqp->_recordedBytes += numBytes;
	code = (uint32_t) fwrite(inDatap, 1, numBytes, aqp->_recordingFilep);
	if (code != numBytes) {
	    NSLog(@"recording error %d should be %d", (int) code, (int) numBytes);
	    aqp->_isRecording = NO;
	    fclose(aqp->_recordingFilep);
	    aqp->_recordingFilep = NULL;
	}
    }
#endif

    NSLog(@"in packetsProc with %d packets aqp=%p rsp=%p", numPackets, aqp, aqp->_radioStreamp);
    packetsCopied = 0;
    bytesCopied = 0;
    for(uint32_t i=0;i<numPackets;i++) {
	int64_t packetOffset = packetsp[i].mStartOffset;
	int64_t packetSize = packetsp[i].mDataByteSize;

	MFANAqStreamPacket *packet = [[MFANAqStreamPacket alloc] init];
	[packet addData: ((char *)inDatap) + packetOffset length: packetSize];
	[aqp->_packetArray addObject: packet];

	packetsCopied++;
	bytesCopied += packetSize;
    }

    // notify target that there's data available.  Note that we have
    // the streamMutex at this point, so our notify callback has to be
    // careful not to do anything directly, but instead fire off a
    // timer or something to pop off stack frames.
    if (aqp->_target != nil)
	[aqp->_target notify: aqp];
}

// Called by radiostream with unparsed data.  Add it in and send it to the parser.
/* static */ int32_t 
MFANAqStream_rsDataProc(void *contextp, RadioStream *radiop, char *bufferp, int32_t nbytes)
{
    OSStatus osStatus;
    std::string *contentTypep;
    AudioFileTypeID fileType;
    uint64_t now;
    float updateRate;
    uint32_t delta;

    pthread_mutex_lock(&_streamMutex);
    if (radiop->isClosed()) {
	pthread_mutex_unlock(&_streamMutex);
	return -1;
    }

    // This adds a reference to the stream.  Do this after the
    // isClosed check, to ensure that we don't add an ARC reference to
    // a freed block of memory.
    MFANAqStream *aqp = (__bridge MFANAqStream *) contextp;

    NSLog(@"in dataproc aqp=%p rsp=%p arsp=%p", aqp, aqp->_radioStreamp, radiop);

    if (aqp->_shuttingDown) {
	pthread_mutex_unlock(&_streamMutex);
	return -1;
    }

    /* update the rate, by dropping it by 50% and adding in half of the new rate;
     * provides exponential decay.
     */
    now = osp_time_ms();
    delta = now - aqp->_lastDataMs;
    if (delta > 2000) {
	updateRate = ((aqp->_lastDataBytes + nbytes) * 1000.0) / delta;
	aqp->_dataRate = aqp->_dataRate * 0.8 +  updateRate * 0.2;
	/* reset stats to indicate starting counting again now */
	aqp->_lastDataMs = now;
	aqp->_lastDataBytes = 0;
    }
    else {
	/* just increment usage, and wait longer to compute rate to get a better
	 * denominator.
	 */
	aqp->_lastDataBytes += nbytes;
    }

    /* we wait until data is streaming in, at which point the
     * radiostream module must know the content type, which we need to
     * create the AudiofileStream parser.
     */
    if (aqp->_audioStreamHandle == 0) {
	/* register callbacks for parsed audiostream data; the PacketsProc
	 * callback will feed the data into the audio queue player.  The
	 * PropertyProc is called first, and provides parameters detected
	 * about the audiostream that we will need to send data to the
	 * audio queue.
	 */
	contentTypep = aqp->_radioStreamp->getContentType();
	fileType = kAudioFileMP3Type;
	if (contentTypep != NULL) {
	    if ( contentTypep->compare(0,8,"audio/mp") == 0) {
		/* includes audio/mpeg and audio/mp3 */
		fileType = kAudioFileMP3Type;
	    }
	    else if (contentTypep->compare(0,9,"audio/aac") == 0) {
		/* includes audio/aac and audio/aacp */
		fileType = kAudioFileAAC_ADTSType;
	    }
	}

	// We already have an ARC reference from the bridge_transfer
	// above, so just make a copy of the pointer
	osStatus = AudioFileStreamOpen ( (__bridge void *) aqp,
					 MFANAqStream_PropertyProc,
					 MFANAqStream_PacketsProc,
					 fileType,
					 &aqp->_audioStreamHandle);
    }

    if (aqp->_shuttingDown) {
	pthread_mutex_unlock(&_streamMutex);
	return 0;
    }

    // For shutdown code that wants to shutdown the stream, we want to
    // be able to check that there are no pending upcalls.  It looks like
    // we actually just wait for the whole pthread to terminate, so
    // we aren't actually checking this yet.
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

// Upcalled from radiosteram when the song being played changes.
int32_t
MFANAqStream_rsControlProc( void *contextp,
			    RadioStream *radiop,
			    RadioStream::EvType event,
			    void *evDatap)
{
    MFANAqStream *aqp;
    NSString *newSong;

    /* watch for upcall arriving after we've been deleted; must return without using
     * any of the memory pointed to be *contextp (that's why _aqMutex is global).
     */
    pthread_mutex_lock(&_streamMutex);
    if (radiop->isClosed()) {
	pthread_mutex_unlock(&_streamMutex);
	return -1;
    }

    // adds an ARC reference.  Note that context may have been freed,
    // and this is a spurious upcall from a closed radiostream, so
    // don't do arc-related operations on the pointer until we've
    // passed the test above.
    aqp = (__bridge MFANAqStream *) contextp;

    if (event == RadioStream::eventSongChanged) {
	RadioStream::EvSongChangedData *songp = (RadioStream::EvSongChangedData *) evDatap;
	if (songp->_song.length() > 0) {
	    newSong = [NSString stringWithUTF8String: songp->_song.c_str()];
	    NSLog(@"- AqPlayer %p newsong is %@ songp=%p songStr=%p",
		  aqp, newSong, songp, songp->_song.c_str());
	    aqp->_currentPlaying = newSong;
	} else {
	    aqp->_currentPlaying = nil;
	}
    }
    else if (event == RadioStream::eventResync) {
	// nothing to do here, but this event occurs if the
	// radiostream connection was reset (it automatically
	// restarts).
	//
	// [aqp shutdown];
    }

    pthread_mutex_unlock(&_streamMutex);
    return 0;
}

// This data stream format information should be available before the
// first upcall of data records.
- (NSString *) getDataFormat {
    uint32_t dataFormatSize;
    OSStatus osStatus;
    AudioStreamBasicDescription dataFormat;

    dataFormatSize = sizeof(dataFormat);
    osStatus = AudioFileStreamGetProperty ( _audioStreamHandle,
					    kAudioFilePropertyDataFormat,
					    (UInt32 *) &dataFormatSize,
					    &dataFormat);
    if (osStatus != 0)
	return @"";


    NSString *rstr;

    if (dataFormat.mFormatID == 'aac ')
	rstr = @"AAC";
    else if (dataFormat.mFormatID == '.mp3')
	rstr = @"MP3";
    else {
	/* unknown, just use the raw encoding as characters */
	rstr = [NSString stringWithFormat: @"%c%c%c%c",
			 (int) (dataFormat.mFormatID>>24) & 0xFF,
			 (int) (dataFormat.mFormatID>>16) & 0xFF,
			 (int) (dataFormat.mFormatID>>8) & 0xFF,
			 (int) dataFormat.mFormatID & 0xFF];
    }
    return rstr;
}

// TODO: get rid of this
- (void) dealloc {
    NSLog(@"in dealloc");
}

- (MFANAqStream *) initWithUrl: (NSString *) url {
    self = [super init];
    if (self) {
	NSLog(@"- AqStream init starts for %p", self);

	_shuttingDown = NO;
	_audioStreamHandle = 0;
	_radioStreamp = nullptr;

	if (!_staticSetup) {
	    _staticSetup = YES;
	    pthread_mutex_init(&_streamMutex, NULL);
	}
	pthread_cond_init(&_pthreadIdleCv, NULL);
	_urlString = url;

	_packetArray = [[NSMutableArray alloc] init];

	_pthreadDone = NO;
	_isRecording = NO;
	_recordingFilep = NULL;

	_haveProperties = NO;

	_dataRate = 0.0;
	_lastDataMs = osp_time_ms();
	_lastDataBytes = 0;

	_activeParseCalls = 0;

	_maxBufferSize = 0x4000;
	_maxPacketCount = 512;

	_streamAttachCounter = 0;

	_pthreadWaiters = 0;

	_radioStreamThread = [[NSThread alloc] initWithTarget: self
					       selector: @selector(playAsync:)
					       object: nil];
	[_radioStreamThread start];

    }
    return self;
}

// The async thread that processes the RadioStream invokes this method
// after the thread is created.
- (void) playAsync: (id) junk
{
    MFANSocketFactory socketFactory;
    MFANAqStream *threadReference = self;

    NSLog(@"in playAsync");
    // Just keep restarting the radiostream until we're told to shut
    // it down.
    while(1) {
	/* initialize basics */
	pthread_mutex_lock(&_streamMutex);
	if (_shuttingDown) {
	    // break with lock held
	    break;
	}

	_radioStreamp = new RadioStream();
	pthread_mutex_unlock(&_streamMutex);

	_radioStreamp->init( &socketFactory,
			     (char *) [_urlString cStringUsingEncoding: NSUTF8StringEncoding],
			     MFANAqStream_rsDataProc,
			     MFANAqStream_rsControlProc,
			     (__bridge void *) self);
	NSLog(@"- aqplayer %p radioplayer done", self);

	// stop upcalls from this instance and release our reference to it.
	pthread_mutex_lock(&_streamMutex);
	if (_radioStreamp != nullptr) {
	    _radioStreamp->close();
	    _radioStreamp = nullptr;
	}
	pthread_mutex_unlock(&_streamMutex);
    }

    // lock still held
    _pthreadDone = YES;
    pthread_mutex_unlock(&_streamMutex);

    pthread_cond_broadcast(&_pthreadIdleCv);

    // this is probably unnecessary, since a local ARC reference
    // should drop the reference count on the target once we leave the
    // scope of the local.
    threadReference = nil;

    pthread_exit(NULL);
}

- (int32_t) attachTarget: (MFANAqStreamTarget *) target {
    pthread_mutex_lock(&_streamMutex);
    _target = target;
    pthread_mutex_unlock(&_streamMutex);

    return 0;
}

- (void) detachTarget {
    pthread_mutex_lock(&_streamMutex);
    _streamAttachCounter++;
    _target = nil;
    pthread_mutex_unlock(&_streamMutex);
}
@end
