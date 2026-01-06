#import <MediaPlayer/MediaPlayer.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudioTypes.h>
#import <CoreFoundation/CoreFoundation.h>

#import "MFANAqPlayer.h"
#import "MFANCGUtil.h"
#include "MFANSocket.h"

#include <stdio.h>
#include <pthread.h>
#include "bufsocket.h"
#include "radiostream.h"

/* The MFANAqPlayer is a multi-stage pipeline to play music from an
 * HTTP stream, with a simple external interface.
 *
 * The init function initializes the object.
 *
 * The play: function starts playing a stream, given a URL.  It uses
 * the radiostream module to handle indirection through playlists and
 * other random junk that people put in radiostation URLs.
 *
 * The pause function pauses the player, whose resume function can
 * restart the player where it left off.
 *
 * The resume function resumes a paused player.
 *
 * The stop function stops the player in a way that it can't be
 * restarted, but can of course be released.
 *
 * The setStateCallback and setSongCallback set callbacks for state
 * changes and new song indications (from those stations that provide
 * icecast information).
 *
 * The getSongIndex function returns which song we're playing.
 *
 * The getCurrentPlaying function returns a string with the contents
 * of the last song indicated, for icecast strings, and otherwise
 * returns a canned "[Unknown]" string.
 *
 * The static MFANSAqPlayer_getUnknownString returns the contents of
 * the aforementioned canned string, so our caller can tell whether we
 * really know the song, by comparing the indicated song with this
 * string.
 *
 * It handles streams consisting of MP3 or AAC data; it doesn't handle
 * ASX data today because I don't *think* that the Audio player can
 * handle that type of data.
 *
 *================================================================
 *
 * The data path is the play function queues a URL for an async
 * pthread, which calls RadioStream::init to start loading the URL.
 * Two callbacks are registered, one for song changes (control), and
 * one for streamed music data.  The class handles play list and other
 * forms of redirection, and also understands icecast format streams,
 * which come with embedded stream labels.
 *
 * Once data arrives from the radiostream module (via rsDataProc), we
 * open an AudioFileStream, which is a packet parser that understands
 * the stream data type that the stream's content type specifies.  The
 * HTTP headers have already been processed by the time the data
 * stream starts.
 *
 * The parser calls back to the PacketsProc and PropertyProc function,
 * and is supplied with data to parse by the radiostream's data proc's
 * calling AudioFileStreamParseBytes.  The parser then calls back to
 * the PropertyProc as soon as the stream is ready to play, at which
 * time the callback creates and creates an audio queue with
 * AudioQueueNewOutput.
 *
 * The PacketsProc callback is called with an integral number of audio
 * packets, and copies them into a buffer, which gets queued to the
 * player.  When the player finishes with the buffer, it calls back to
 * the handleOutput callback, returning the buffer, where we put it on
 * a free list until the next PacketsProc callback sends more data.
 *
 * Stopping this mess is sorta hard, since you'll crash if you free
 * everything while there are outstanding callbacks, and of course,
 * Apple doesn't really document how to shutdown an AudioStream.
 *
 * When our stop function is called, we stop the radiostream first,
 * but of course there may be outstanding upcalls of radiostream data.
 * If the stream gets closed, the rsDataProc will catch that on the
 * upcall and return.  If not closed at the start, it holds a
 * reference to the MFANAqPlayer so it doesn't get freed during the
 * upcalls it generates.  It returns as soon as it notices the closed
 * flag getting set.
 *
 * We can't detect if HandleOutput is called while we're trying to
 * shutdown, so we're just hoping that we won't get a pending upcall
 * after AudioQueueStop / AudioQueueDispose return to us, because
 * after that happens, our MFANAqPlayer may be free; I don't think
 * that the AudioQueue code is actually holding a reference to our
 * MFANAqPlayer.  This is an area where Apple simply doesn't document
 * a suitable protocol for a clean shutdown.
 *
 ================================================================
 *
 * The radiostream is initialized with a BufGen factory, which creates
 * TCP sockets to specified host/port pairs.  For iOS, we need to
 * provide a factory that generates MFANSocket structures, since
 * regular Berkeley sockets don't work on cellular networks (starting
 * in iOS 10.2, apparently).  MFANSockets have the same BufGen
 * interface as our buffered socket wrapped BufSocket, but are
 * implemented on top of CFStreams instead.
 *
 * Thanks Apple for making BSD sockets unusable, and doing so without
 * any warning.
 */

/* types:
   kAudioFileAIFFType                              = 'AIFF',
   kAudioFileAIFCType                              = 'AIFC',
   kAudioFileWAVEType                              = 'WAVE',
   kAudioFileSoundDesigner2Type    = 'Sd2f',
   kAudioFileNextType                              = 'NeXT',
   kAudioFileMP3Type                               = 'MPG3',       // mpeg layer 3
   kAudioFileMP2Type                               = 'MPG2',       // mpeg layer 2
   kAudioFileMP1Type                               = 'MPG1',       // mpeg layer 1
   kAudioFileAC3Type                               = 'ac-3',
   kAudioFileAAC_ADTSType                  = 'adts',
   kAudioFileMPEG4Type             = 'mp4f',
   kAudioFileM4AType               = 'm4af',
   kAudioFileM4BType               = 'm4bf',
   kAudioFileCAFType                               = 'caff',
   kAudioFile3GPType                               = '3gpp',
   kAudioFile3GP2Type                              = '3gp2', 
   kAudioFileAMRType                               = 'amrf'

   Note that it seems that MP3 streams are of type MP3Type and AAC
   sterams are AAC_ADTSType.
 */

static const uint32_t MFANAqPlayer_nBuffers = 16;

/* global function */
NSString *
MFANAqPlayer_getUnknownString()
{
    return @"[Unknown]";
}

@implementation MFANAqPlayer {
    int32_t _refCount;
    MFANAqPlayer *_refHeld;

    AudioFileStreamID _audioStreamHandle;
    AudioQueueRef _audioQueuep;
    RadioStream *_radioStreamp;
    AudioStreamBasicDescription _dataFormat;	/* detailed data format info from parser */
    uint32_t _activeParseCalls;
    pthread_cond_t _activeParseCallCond;
    BOOL _activeParseCallWaiters;

    BOOL _isRecording;
    FILE *_recordingFilep;
    uint64_t _recordedBytes;
    uint32_t _recordingYear;

    /* who to notify if recording stops */
    id _recordingWho;
    SEL _recordingSel;

    NSString *_currentPlaying;

    /* our choices for max # of bytes in a buffer, and packets in a buffer */
    uint32_t _maxBufferSize;
    uint32_t _maxPacketCount;

    /* tracking playing; the stupid AudioQueue property listener
     * doesn't notice if you actually pause playing (another Apple
     * POS).
     */
    uint32_t _lastReturnedMs;	/* time an audio packet was last returned to us */
    uint32_t _lastDataMs;	/* last time data was received */
    uint32_t _lastDataBytes;	/* bytes since lastDataMs */
    uint32_t _formatId;		/* audio format ID */
    float _dataRate;		/* estimated data rate */
    NSTimer *_spyTimer;		/* timer firing every second or so */
    NSTimer *_shutdownTimer;	/* used for delayed shutdown work */
    BOOL _lastUpcalledIsPlaying;
    BOOL _isPlaying;
    BOOL _shouldRestart;
    BOOL _upcalledShutdownState;
    SEL _stateCallbackSel;	/* who to notify, if anyone */
    id _stateCallbackObj;	/* ditto */
    SEL _songCallbackSel;	/* who to notify, if anyone */
    id _songCallbackObj;	/* ditto */

    /* for debugging, make sure that we don't get packet data before we complete
     * the metadata information callback.
     */
    BOOL _listenDone;

    /* buffers start off available, and get filled by an asynchronous
     * process as our MFANAqPlayer_PacketsProc function is called by
     * the stream parser and sent to the AudioQueue player.
     *
     * They get played by the AudioQueue player, and then returned to
     * us by the AudioPlayer calling our MFANAqPlayer_handleOutput
     * function.  Once returned, they wait in the available queue
     * until we have more data to stick in them, at which point we
     * fill them again and supply them back to the AudioQueue.
     *
     * The main edge condition is if the parser sends us data after
     * we've already sent all of our available packets to the
     * AudioQueue (availCount is 0). In this case, the parser thread
     * blocks until the AudioQueue finishes up a buffer.
     */
    AudioQueueBufferRef _buffersp[MFANAqPlayer_nBuffers];
    uint32_t _availIx;
    uint32_t _availCount;
    BOOL _availEmptyWaiter;		/* parser is waiting for available buffers */
    BOOL _availFullWaiter;		/* someone's waiting for space to add a new buffer */
    BOOL _stopped;
    BOOL _shutdown;
    BOOL _paused;
    BOOL _pthreadDone;

    BOOL _rsIdle;
    NSThread *_radioStreamThread;

    /* state to keep track of main radiostream pthread */
    pthread_mutex_t _rsMutex;
    pthread_cond_t _aqCond;
    pthread_cond_t _rsCond;

    NSString *_urlString;

    /* rather than have a huge local, this array stores the packet
     * descriptors for the audio buffer we're about to queue.  Its
     * contents are valid only for the duration of the incoming
     * packets callback.
     */
    AudioStreamPacketDescription *_packetsp;
}

int _songCount = 0;

/* we're getting callbacks from a C++ library, so the callbacks
 * aren't holding reference counts on the MFANAqPlayer object.
 *
 * To get rid of an MFANAqPlayer, we grab the global aqMutex, and
 * synchronously deletes the radiostream (or other objects) that make
 * upcalls.  The deleted (or whatever) flag gets set while holding the
 * aqMutex, and the callback checks the flag under the same lock.
 *
 * The callback function wants to increment a ref count under the same
 * aqMutex after checking for the deleted flag in the upcalling
 * object.  However, Objective C's ARC doesn't let us access to the
 * ref count, so instead, if the ref count is non-zero, we set a
 * pointer in refHeld which holds the ARC ref count on our own
 * structure.
 *
 * Also note that when init has been called, but our caller hasn't
 * called play yet, our caller may not call shutdownAudio before just
 * dropping a pointer to the MFANAqPlayer.  To ensure that we will
 * free the structure properly in that case, we delay setting _refHeld
 * until play is called, even though the refCount > 0.  The play
 * function calls checkRefNL, which set refHeld if the refCount > 0
 * (which it will be).  This is probably a stupid feature, but c'est
 * la vie.
 */
- (void) holdNL
{
    _refCount++;
    _refHeld = self;
}

- (void) releaseNL
{
    if (--_refCount == 0) {
	_refHeld = nil;
    }
}

- (void) checkRefNL
{
    if (_refCount > 0)
	_refHeld = self;
}

/* one per system, i.e. static variables; we keep aqMutex static so
 * that we can reference it for cancellation tests even if our aqp has
 * been freed as part of a shutdown.
 */
int _staticSetup=0;
pthread_mutex_t _aqMutex;

/* this is called by the AudioQueue package when it finishes playing some music, telling
 * us to make available for filling with data a buffer we'd been using.
 *
 * This can be called on threads other than the main thread.
 */
void
MFANAqPlayer_handleOutput( void *acontextp,
			   AudioQueueRef aqRefp,
			   AudioQueueBufferRef bufRefp)
{
    MFANAqPlayer *aqp = (__bridge MFANAqPlayer *) acontextp;

    /* push the buffer for reuse */
    pthread_mutex_lock(&_aqMutex);
    [aqp pushBuffer: bufRefp];
    aqp->_lastReturnedMs = osp_time_ms();
    pthread_mutex_unlock(&_aqMutex);
}

/* called on the main thread to create a new player */
- (MFANAqPlayer *) init
{
    self = [super init];
    if (self) {
	NSLog(@"- aqplayer init starts for %p", self);

	/* refHeld starts off nil in case someone just deletes the
	 * object before using it at all, and doesn't shut it down
	 * first.  Note that until play is called, you can delete this
	 * object by dropping a pointer to it, or by calling
	 * shutdownAudio (and dropping pointer to it).  Afterwards,
	 * you must call shutdownAudio before dropping pointer, since
	 * refHeld will be set.
	 */
	_refCount = 1;
	_refHeld = nil;

	_recordingWho = nil;
	_recordingSel = nil;

	if ( !_staticSetup) {
	    pthread_mutex_init(&_aqMutex, NULL);
	    _staticSetup = 1;
	}
	pthread_mutex_init(&_rsMutex, NULL);
	pthread_cond_init(&_aqCond, NULL);
	pthread_cond_init(&_rsCond, NULL);

	pthread_cond_init(&_activeParseCallCond, NULL);
	_urlString = nil;
	_rsIdle = YES;
	_spyTimer = nil;
	_shutdownTimer = nil;

	_isRecording = NO;
	_recordingFilep = NULL;

	_dataRate = 0.0;
	_lastDataMs = osp_time_ms();
	_lastDataBytes = 0;
	_lastReturnedMs = osp_time_ms();
	_lastUpcalledIsPlaying = NO;
	_upcalledShutdownState = NO;
	_isPlaying= NO;
	_stateCallbackObj = nil;

	_activeParseCallWaiters = NO;
	_activeParseCalls = 0;
	_availIx = 0;
	_availCount = 0;
	_stopped = NO;
	_shouldRestart = YES;
	_paused = NO;
	_shutdown = NO;
	_availEmptyWaiter = NO;
	_availFullWaiter = NO;
	_listenDone = NO;
	_packetsp = NULL;
	_audioStreamHandle = 0;
	_currentPlaying = MFANAqPlayer_getUnknownString();
	_songCount++;

	NSLog(@"- aqplayer init2 starts for %p cplaying=%p", self, _currentPlaying);

	_spyTimer = [NSTimer scheduledTimerWithTimeInterval: 1.0
			     target:self
			     selector:@selector(spyMonitor:)
			     userInfo:nil
			     repeats: YES];

	_maxBufferSize = 0x4000;
	_maxPacketCount = 512;

	_pthreadDone = 0;
	_radioStreamThread = [[NSThread alloc] initWithTarget: self
					       selector: @selector(playAsync:)
					       object: nil];
	[_radioStreamThread start];

    }
    return self;
}

/* called on main thread to see if the music is actually playing, i.e. if buffers
 * of music are being returned to our free pool.
 */
- (void) spyMonitor: (id) junk
{
    uint32_t now;

    pthread_mutex_lock(&_aqMutex);

    now = osp_time_ms();
    if (now - _lastReturnedMs > 2500) {
	_isPlaying = NO;
    }
    else {
	_isPlaying = YES;
    }

    if (now - _lastReturnedMs > 10000) {
	[self shutdownAudio];

	/* and force new upcall, so player detects aqPlayer has stopped; only do this once
	 * for a given aqplayer.
	 */
	if (!_upcalledShutdownState) {
	    _upcalledShutdownState = YES;
	    _lastUpcalledIsPlaying = !_isPlaying;
	}
    }

    if (_stateCallbackObj != nil && _lastUpcalledIsPlaying != _isPlaying) {
	_lastUpcalledIsPlaying = _isPlaying;
	pthread_mutex_unlock(&_aqMutex);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
	dispatch_async(dispatch_get_main_queue(), ^{
        [self->_stateCallbackObj performSelector: self->_stateCallbackSel withObject: nil];
	    });
#pragma clang diagnostic pop
	return;
    }
    else {
	pthread_mutex_unlock(&_aqMutex);
    }
}

- (void) notifyStopped
{
    id who;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    if (_recordingWho != nil) {
	who = _recordingWho;
	_recordingWho = nil;
	[who performSelector: _recordingSel];
    }
#pragma clang diagnostic pop
}

/* must be called with the aqLock held; drops lock temporarily;
 * Called from various threads.
 */
- (void) shutdownAudio
{
    NSLog(@"- in shutdown audio for aqp=%p", self);

    /* check if we've started a shutdown procedure */
    if (_shutdown) {
	NSLog(@"- shutdownAudio for stopped player %p", self);
	return;
    }

    /* if we're called on the wrong thread, move to the right one.  If we don't
     * do this, the NSTimer stuff doesn't work.
     */
    if (![NSThread isMainThread]) {
	NSLog(@" - shutdownAudio bouncing to main thread");
	dispatch_async(dispatch_get_main_queue(), ^{
		[self shutdownAudio];
	    });
	return;
    }

    if (_isRecording) {
	[self stopRecording];
	[self notifyStopped];
    }

    _shutdown = YES;
    _stopped = YES;

    /* we're shutting down the audio queue, thus it won't be paused afterwards */
    _paused = NO;

    if (_radioStreamp != NULL) {
	_radioStreamp->close();
	_radioStreamp = NULL;
    }

    /* stop the audio queue and dispose of it; can't set "immediate" to true,
     * since the AudioQueue system deadlocks if you do.
     */
    if (_audioQueuep) {
	AudioQueueStop(_audioQueuep, /* immediate */ 0);
	AudioQueueDispose(_audioQueuep, /* immediate */ 0);
    }

    /* by the time there are no outstanding parser calls, we know that
     * no one is adding more buffers to the audio queue.
     *
     * I'm not sure how we can guarantee the audio queue isn't still
     * running, but perhaps having called AudioQueueStop and
     * AudioQueueDispose guarantees that it has stopped.
     *
     * Well, experience shows that the player can still return packets
     * to us, so we're going to stop the audio, and then wait a few
     * seconds before actually allowing the structure to be freed.
     */
    while(_activeParseCalls) {
	_activeParseCallWaiters = YES;

	/* if someone's waiting for a buffer, wake them up so they see we're
	 * shutdown.
	 */
	if (_availEmptyWaiter) {
	    _availEmptyWaiter = NO;
	    pthread_cond_broadcast(&_aqCond);
	}

	pthread_cond_wait(&_activeParseCallCond, &_aqMutex);
    }

    /* leave _audioQueuep pointer set in case we need it during parse call shutdown */
    _audioQueuep = NULL;

    /* close the parser handle */
    AudioFileStreamClose(_audioStreamHandle);
    _audioStreamHandle = 0;

    _shutdownTimer = [NSTimer scheduledTimerWithTimeInterval: 10.0
			      target:self
			      selector:@selector(shutdownAudioPart2:)
			      userInfo:nil
			      repeats: NO];
    NSLog(@"- shutdown starts timer %p", self);
}

/* the timer itself should keep our MFANAqPlayer allocated until it fires */
- (void) shutdownAudioPart2: (id) junk
{
    int32_t i;

    pthread_mutex_lock(&_aqMutex);

    NSLog(@"- shutdown part2 starts=%p", self);

    /* free packets array; these are all recreated when PropertyProc is called when 
     * the stream is recreated.
     */
    free(_packetsp);
    _packetsp = NULL;

    /* free buffers */
    for(i=0;i<MFANAqPlayer_nBuffers;i++) {
	if (_buffersp[i]) {
	    AudioQueueFreeBuffer(_audioQueuep, _buffersp[i]);
	    _buffersp[i] = NULL;
	}
    }

    /* and reset queue of buffers */
    _availCount = 0;
    _availIx = 0;

    if (_spyTimer) {
	[_spyTimer invalidate];
	_spyTimer = nil;
    }

    /* release of reference set by init function; from this point on,
     * if someone drops the last pointer to our structure, our structure
     * will actually get freed.
     */
    [self releaseNL];
    NSLog(@"- release done in shutdown, refct=%d ref=%p", _refCount, _refHeld);

    pthread_mutex_unlock(&_aqMutex);

    pthread_cond_broadcast(&_aqCond);	/* just in case */

    NSLog(@"- done with shutdown audio for aqp=%p", self);
}

/* called when we switch away from this radio station from GUI */
- (void) stop
{
    /* make sure we stop recording cleanly, so that we can write the
     * file trailers properly.
     */
    if (_isRecording) {
	[self stopRecording];
    }

    pthread_mutex_lock(&_aqMutex);

    /* code may hang if we try to stop it again */
    if (_stopped) {
	pthread_mutex_unlock(&_aqMutex);
	return;
    }

    _stopped = YES;
    _shouldRestart = NO;
    if (_availEmptyWaiter) {
	_availEmptyWaiter = NO;
	pthread_cond_broadcast(&_aqCond);
    }
    pthread_mutex_unlock(&_aqMutex);

    if (_radioStreamp != NULL) {
	_radioStreamp->close();
	_radioStreamp = NULL;
    }

    pthread_mutex_lock(&_aqMutex);
    [self shutdownAudio];
    pthread_mutex_unlock(&_aqMutex);
}

/* called when someone presses pause */
- (void) pause
{
    pthread_mutex_lock(&_aqMutex);
    if (_stopped) {
	pthread_mutex_unlock(&_aqMutex);
	return;
    }

    if (_audioQueuep) {
	AudioQueuePause(_audioQueuep);
    }
    _paused = YES;
    _shouldRestart = NO;
    pthread_mutex_unlock(&_aqMutex);
}

/* called when someone presses resume */
- (void) resume
{
    pthread_mutex_lock(&_aqMutex);
    if (_stopped) {
	pthread_mutex_unlock(&_aqMutex);
	return;
    }

    if (_audioQueuep) {
	AudioQueueStart(_audioQueuep, NULL);
    }
    _paused = NO;
    _shouldRestart = YES;
    pthread_mutex_unlock(&_aqMutex);
}

- (void) setStateCallback: (id) callbackObj  sel: (SEL) callbackSel
{
    pthread_mutex_lock(&_aqMutex);
    _stateCallbackObj = callbackObj;
    _stateCallbackSel = callbackSel;
    pthread_mutex_unlock(&_aqMutex);
}

- (void) setSongCallback: (id) callbackObj  sel: (SEL) callbackSel
{
    pthread_mutex_lock(&_aqMutex);
    _songCallbackObj = callbackObj;
    _songCallbackSel = callbackSel;
    pthread_mutex_unlock(&_aqMutex);
}

- (BOOL) isPlaying
{
    /* note that the k...isRunning property doesn't notice
     * pause/resume, and so is useless.  We track output buffer handle
     * returns instead, using a timer.
     */
    return _isPlaying;
}

/* this function is called by the AudioFileStreamParsePackets function, once
 * enough data has been received that we can parse the packet info.  Since the
 * radiostream data proc grabs the aqMutex, we don't have to do it here, since
 * our caller did it for us.
 */
void
MFANAqPlayer_PropertyProc( void *contextp,
			   AudioFileStreamID audioFilep,
			   AudioFileStreamPropertyID propertyId,
			   UInt32 * ioFlags)
{
    MFANAqPlayer *aqp = (__bridge MFANAqPlayer *) contextp;
    OSStatus osStatus;
    uint32_t dataFormatSize;
    uint32_t i;

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

	/* create a playing queue */
	osStatus = AudioQueueNewOutput ( &aqp->_dataFormat,
					 MFANAqPlayer_handleOutput,
					 (__bridge void *) aqp,
					 /* runloop */ NULL,
					 /* ioop mode */ kCFRunLoopCommonModes,
					 /* flags */ 0,
					 &aqp->_audioQueuep);
	if (osStatus != 0) {
	    NSError *error = [NSError errorWithDomain: NSOSStatusErrorDomain
				      code: osStatus
				      userInfo: nil];
	    NSLog(@"! AudioQueue error %@", [error description]);
	}

	/* allocate packet descriptors used for sending the current parsed packet */
	aqp->_packetsp = ((AudioStreamPacketDescription *)
			      malloc(aqp->_maxPacketCount *
				     sizeof(AudioStreamPacketDescription)));

	/* and allocate the buffers to share */
	for(i=0;i<MFANAqPlayer_nBuffers;i++) {
	    AudioQueueAllocateBuffer( aqp->_audioQueuep,
				      aqp->_maxBufferSize,
				      &aqp->_buffersp[i]);
	    [aqp pushBuffer: aqp->_buffersp[i]];
	}

	/* turn up the volume; it is nice that this poorly documented
	 * interface defaults to playing silently, so that if you
	 * forget this step, you won't hear anything, or have a clue
	 * why.  Thanks, Apple.
	 */
	Float32 gain = 1.0;
	osStatus = AudioQueueSetParameter ( aqp->_audioQueuep,
					    kAudioQueueParam_Volume,
					    gain);

	aqp->_listenDone = YES;

	osStatus = AudioQueueStart( aqp->_audioQueuep, NULL);
    }
}

/* this function is called by the parser when a block of data is available.
 * We find a buffer that has been returned from the audioQueue, and fill it with
 * data and send it back to the queue
 *
 * We expect the parser to call this before delivering any packet data, but if not,
 * we note this.
 *
 * This is called back from the rsDataProc, so it is rsDataProc that
 * gets the aqMutex lock.
 */
void
MFANAqPlayer_PacketsProc( void *contextp,
			  UInt32 numBytes,
			  UInt32 numPackets,
			  const void *inDatap,
			  AudioStreamPacketDescription *packetsp)
{
    MFANAqPlayer *aqp = (__bridge MFANAqPlayer *) contextp;
    AudioQueueBufferRef bufRefp = NULL;
    uint32_t bytesCopied;
    uint32_t packetsCopied;
    uint32_t i;
    OSStatus osStatus;
    int32_t code;

    if (!aqp->_listenDone) {
	NSLog(@"! MFANAqPlayer data received before properties callback");
	return;
    }

    if (numPackets == 0) {
	NSLog(@"- PacketsProc shutting down audioqueue due to no packets");
	AudioQueueStop(aqp->_audioQueuep, false);
	return;
    }

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

    bufRefp = NULL;
    packetsCopied = 0;
    bytesCopied = 0;
    for(i=0;i<numPackets;i++) {
	if (aqp->_stopped) {
	    if (bufRefp != NULL) {
		[aqp pushBuffer: bufRefp];
	    }
	    return;
	}

	int64_t packetOffset = packetsp[i].mStartOffset;
	int64_t packetSize = packetsp[i].mDataByteSize;

	if (aqp->_maxBufferSize - bytesCopied < packetSize) {
	    /* flush the buffer */
	    bufRefp->mAudioDataByteSize = bytesCopied;
	    osStatus = AudioQueueEnqueueBuffer ( aqp->_audioQueuep,
						 bufRefp,
						 packetsCopied,
						 aqp->_packetsp);
	    if (osStatus != 0) {
		/* buffer didn't get queued so we have to avoid losing it */
		NSLog(@"! aqplayer enqueue failed, repushing");
		[aqp pushBuffer: bufRefp];
	    }
	    bufRefp = NULL;
	}

	if (!bufRefp) {
	    /* get an available packet; this adjusts _availIx and
	     * _availCount to indicate one fewer buffers available to
	     * the parser.
	     *
	     * Note that popBuffer drops the _aqMutex if necessary.
	     */
	    bufRefp = [aqp popBuffer];
	    if (!bufRefp || aqp->_stopped) {
		/* we really only come in here if we get a callback from the parser
		 * while we're stopping the MFANAqPlayer.
		 */
		return;
	    }

	    /* if we've been able to get a buffer, then someone must have returned a
	     * buffer, so we can update this.  The AudioQueue might have 10 or more
	     * seconds of music queued, so noting the time here avoids the startup
	     * transient where no buffer is returned for a period.
	     */
	    aqp->_lastReturnedMs = osp_time_ms();

	    bytesCopied = 0;
	    packetsCopied = 0;
	}

	/* otherwise, we have enough room, and copy the packet into the current buffer */
	memcpy( (char *)bufRefp->mAudioData+bytesCopied,
		(char *)inDatap+packetOffset,
		(size_t) packetSize);
	aqp->_packetsp[packetsCopied] = packetsp[i];
	aqp->_packetsp[packetsCopied].mStartOffset = bytesCopied;
	packetsCopied++;
	bytesCopied += packetSize;
    }

    /* when we get here, we may have been accumulating packets for one more buffer; send it now */
    if (bytesCopied > 0) {
	bufRefp->mAudioDataByteSize = bytesCopied;
	osStatus = AudioQueueEnqueueBuffer ( aqp->_audioQueuep,
					     bufRefp,
					     packetsCopied,
					     aqp->_packetsp);
	if (osStatus != 0) {
	    /* buffer didn't get queued so we have to avoid losing it */
	    NSLog(@"! aqplayer enqueue failed, pushed");
	    [aqp pushBuffer: bufRefp];
	}
	bufRefp = NULL;
    }
}

/* static */ int32_t 
MFANAqPlayer_rsDataProc(void *contextp, RadioStream *radiop, char *bufferp, int32_t nbytes)
{
    OSStatus osStatus;
    std::string *contentTypep;
    AudioFileTypeID fileType;
    uint32_t now;
    float updateRate;
    uint32_t delta;

    MFANAqPlayer *aqp = (__bridge MFANAqPlayer *) contextp;

    pthread_mutex_lock(&_aqMutex);
    if (radiop->isClosed()) {
	pthread_mutex_unlock(&_aqMutex);
	return -1;
    }

    if (aqp->_stopped) {
	pthread_mutex_unlock(&_aqMutex);
	return -1;
    }

    /* prevent us from getting freed while running the callback */
    [aqp holdNL];

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

	osStatus = AudioFileStreamOpen ( (__bridge void *) aqp,
					 MFANAqPlayer_PropertyProc,
					 MFANAqPlayer_PacketsProc,
					 fileType,
					 &aqp->_audioStreamHandle);
    }

    if (aqp->_stopped) {
	[aqp releaseNL];
	pthread_mutex_unlock(&_aqMutex);
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
    if (aqp->_activeParseCallWaiters && aqp->_activeParseCalls == 0) {
	pthread_cond_broadcast(&aqp->_activeParseCallCond);
    }

    /* drop matching hold from above */
    [aqp releaseNL];

    pthread_mutex_unlock(&_aqMutex);
    return 0;
}

int32_t
MFANAqPlayer_rsControlProc( void *contextp,
			    RadioStream *radiop,
			    RadioStream::EvType event,
			    void *evDatap)
{
    MFANAqPlayer *aqp;
    NSString *newSong;

    aqp = (__bridge MFANAqPlayer *) contextp;

    /* watch for upcall arriving after we've been deleted; must return without using
     * any of the memory pointed to be *contextp (that's why _aqMutex is global).
     */
    pthread_mutex_lock(&_aqMutex);
    if (radiop->isClosed()) {
	pthread_mutex_unlock(&_aqMutex);
	return -1;
    }

    [aqp holdNL];

    if (event == RadioStream::eventSongChanged) {
	RadioStream::EvSongChangedData *songp = (RadioStream::EvSongChangedData *) evDatap;
	if (songp->_song.length() > 0) {
	    newSong = [NSString stringWithUTF8String: songp->_song.c_str()];
	    NSLog(@"- AqPlayer %p newsong is %@ songp=%p songStr=%p",
		  aqp, newSong, songp, songp->_song.c_str());
	    if ( newSong != nil &&
		 (aqp->_currentPlaying == nil ||
		  ![newSong isEqualToString: aqp->_currentPlaying])) {
		aqp->_currentPlaying = newSong;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
		_songCount++;
		if (aqp->_songCallbackObj) {
		    dispatch_async(dispatch_get_main_queue(), ^{
			    [aqp->_songCallbackObj
				performSelector: aqp->_songCallbackSel
				withObject: nil];
			});
		}
#pragma clang diagnostic pop
	    }
	}
    }
    else if (event == RadioStream::eventResync) {
	/* mark the stream as shutdown */
	[aqp shutdownAudio];
    }

    [aqp releaseNL];	/* from above */

    pthread_mutex_unlock(&_aqMutex);
    return 0;
}

- (void) play: (NSString *) urlString
{
    pthread_mutex_lock(&_aqMutex);
    [self checkRefNL];
    pthread_mutex_unlock(&_aqMutex);

    /* kick this off on a different dispatcher, since it reads data from
     * the network synchronously.
     */
    _stopped = NO;
    _shouldRestart = YES;

    pthread_mutex_lock(&_rsMutex);
    NSLog(@"- aqplayer play %p for %@ old=%@", self, urlString, _urlString);
    if (_urlString == nil) {
	_urlString = urlString;
	pthread_cond_broadcast(&_rsCond);
    }
    pthread_mutex_unlock(&_rsMutex);
}

- (void) playAsync: (id) junk
{
    NSString *urlString;
    MFANSocketFactory socketFactory;

    while(!_stopped) {
	pthread_mutex_lock(&_rsMutex);
	/* wait for an async request */
	NSLog(@"- aqplayer %p async thread top of loop _urlString=%@", self, _urlString);
	while (_urlString == nil) {
	    _rsIdle = YES;
	    NSLog(@"- aqplayer blocking %p", self);
	    pthread_cond_wait(&_rsCond, &_rsMutex);
	}
	urlString = _urlString;
	_urlString = nil;
	_rsIdle = NO;
	pthread_mutex_unlock(&_rsMutex);

	/* initialize basics */
	pthread_mutex_lock(&_aqMutex);
	if (_stopped) {
	    pthread_mutex_unlock(&_aqMutex);
	    break;
	}
	_packetsp = NULL;
	_listenDone = NO;

	_radioStreamp = new RadioStream();
	pthread_mutex_unlock(&_aqMutex);

	_radioStreamp->init( &socketFactory,
			     (char *) [urlString cStringUsingEncoding: NSUTF8StringEncoding],
			     MFANAqPlayer_rsDataProc,
			     MFANAqPlayer_rsControlProc,
			     (__bridge void *) self);
	NSLog(@"- aqplayer %p radioplayer done", self);
    }

    [self shutdownAudio];

    _pthreadDone = 1;
    pthread_exit(NULL);
}

/* called with _aqMutex held, to add a buffer to the free list of buffers
 * More can be sent to us than we have room for, in which case we block
 * until room has freed up.
 */
- (void) pushBuffer: (AudioQueueBufferRef) item
{
    uint32_t ix;

    while (_availCount >= MFANAqPlayer_nBuffers) {
	_availFullWaiter = YES;
	pthread_cond_wait(&_aqCond, &_aqMutex);
    }

    ix = _availIx+_availCount;
    if (ix >= MFANAqPlayer_nBuffers)
	ix -= MFANAqPlayer_nBuffers;
    _buffersp[ix] = item;

    _availCount++;
    if (_availEmptyWaiter) {
	_availEmptyWaiter = NO;
	pthread_cond_broadcast(&_aqCond);
    }
}

/* called with _aqMutex held */
- (AudioQueueBufferRef) popBuffer
{
    AudioQueueBufferRef item;

    while (_availCount <= 0) {
	if (_stopped) {
	    return NULL;
	}
	_availEmptyWaiter = YES;
	pthread_cond_wait(&_aqCond, &_aqMutex);
    }
    _availCount--;
    item = _buffersp[_availIx];
    if (++_availIx >= MFANAqPlayer_nBuffers)
	_availIx = 0;
    if (_availFullWaiter) {
	_availFullWaiter = NO;
	pthread_cond_broadcast(&_aqCond);
    }

    return item;
}

- (NSString *)getCurrentPlaying
{
    return _currentPlaying;
}

- (int) getSongIndex
{
    return _songCount;
}

- (BOOL) stopped
{
    return _stopped;
}

- (void) dealloc {
    NSLog(@"- dealloc of aqplayer %p", self);
}

- (BOOL) shouldRestart
{
    return _shouldRestart;
}

- (NSString *) getEncodingType
{
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

- (NSString *) getStreamUrl
{
    std::string *urlStringp;

    if (_radioStreamp) {
	urlStringp = _radioStreamp->getStreamUrl();
	if (urlStringp) 
	    return [NSString stringWithUTF8String: urlStringp->c_str()];
    }

    return @"[None]";
}

- (float) dataRate
{
    return _dataRate;
}

/* returns true if able to start */
- (BOOL) startRecordingFor: (id) who sel: (SEL) sel
{
    struct tm tmStruct;
    time_t myTime;
    NSString *file;
    const char *extensionp;

    if (_isRecording)
	return YES;

    _recordingWho = who;
    _recordingSel = sel;

    myTime = time(0);
    localtime_r(&myTime, &tmStruct);
    if (_dataFormat.mFormatID == 'aac ')
	extensionp = "aac";	// XXXX m4a
    else
	extensionp = "mp3";
	    
    _recordingYear = tmStruct.tm_year + 1900;

    file = [NSString stringWithFormat: @"Recording-%d-%d-%d-%d-%d-%d.%s",
		     tmStruct.tm_year+1900,
		     tmStruct.tm_mon+1,	/* 0 is January */
		     tmStruct.tm_mday,
		     tmStruct.tm_hour,
		     tmStruct.tm_min,
		     tmStruct.tm_sec,
		     extensionp];

    _recordingFilep = fopen( [fileNameForDoc(file) cStringUsingEncoding:
						NSUTF8StringEncoding], "w");
    if (!_recordingFilep)
	return NO;

    _isRecording = YES;
    _recordedBytes = 0;

    if (_dataFormat.mFormatID == 'aac ') {
	/* reserve enough room for an 0x20 byte ftyp atom, and for the header
	 * of the mdat atom (8 bytes)
	 */
	// XXXX
	// fseek(_recordingFilep, 0x28, SEEK_SET);
    }

    return YES;
}

- (int32_t) stopRecording
{
    char tbuffer[130];
    char *tp;
    int32_t code;

    if (!_isRecording)
	return 0;

    if (_dataFormat.mFormatID == 'aac ') {
	/* nothing to do here, since our file format is just raw AAC records */
    }
    else {
	/* put out old style MP3 trailer (easiest to do) */
	memset(tbuffer, 0, sizeof(tbuffer));
	tp = tbuffer;

	*tp++ = 'T';	/* id3v1 tag */
	*tp++ = 'A';
	*tp++ = 'G';

	strcpy(tp, "Unknown title");
	tp += 30;
	strcpy(tp, "Unknown artist");
	tp += 30;
	strcpy(tp, "Unknown album");
	tp += 30;

	/* write out _recordingYear */
	*tp++ = (_recordingYear/1000) + '0';	/* fails at year 10000 */
	*tp++ = ((_recordingYear/100) % 10) + '0';
	*tp++ = ((_recordingYear/10) % 10) + '0';
	*tp++ = (_recordingYear % 10) + '0';

	/* comment */
	strcpy(tp, "Saved by AudioGalaxy");
	tp += 30;

	*tp++ = 96;	/* big band :-) */

	/* write out MP3 v1 tag */
	fwrite(tbuffer, 1, 128, _recordingFilep);
    }

    code = fclose(_recordingFilep);

    _recordingFilep = NULL;
    _isRecording = NO;

    [self notifyStopped];

    return code;
}

- (BOOL) recording
{
    return _isRecording;
}

@end
