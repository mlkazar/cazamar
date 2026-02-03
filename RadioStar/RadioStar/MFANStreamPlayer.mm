#import <MediaPlayer/MediaPlayer.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudioTypes.h>
#import <CoreFoundation/CoreFoundation.h>

#import "MFANStreamPlayer.h"
#import "MFANCGUtil.h"
#include "MFANSocket.h"

#include <stdio.h>
#include <pthread.h>
#include "bufsocket.h"
#include "radiostream.h"

/* The MFANStreamPlayer is a multi-stage pipeline to play music from an
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
 * The static MFANSStreamPlayer_getUnknownString returns the contents of
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
 * reference to the MFANStreamPlayer so it doesn't get freed during the
 * upcalls it generates.  It returns as soon as it notices the closed
 * flag getting set.
 *
 * We can't detect if HandleOutput is called while we're trying to
 * shutdown, so we're just hoping that we won't get a pending upcall
 * after AudioQueueStop / AudioQueueDispose return to us, because
 * after that happens, our MFANStreamPlayer may be free; I don't think
 * that the AudioQueue code is actually holding a reference to our
 * MFANStreamPlayer.  This is an area where Apple simply doesn't document
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

static const uint32_t MFANStreamPlayer_nBuffers = 16;

/* global function */
NSString *
MFANStreamPlayer_getUnknownString()
{
    return @"[Unknown]";
}

@implementation MFANStreamPlayer {
    AudioQueueRef _audioQueue;
    AudioStreamBasicDescription _dataFormat;	/* detailed data format info from parser */
    MFANAqStream *_aqStream;

    // used by the playAsync thread, but available here for canceling the
    // read performed by the thread.
    MFANAqStreamReader *_streamReader;

    // next position in stream to read
    NSString *_currentPlaying;

    // our choices for max # of bytes in a buffer, and packets in a
    // buffer.
    uint32_t _maxBufferSize;
    uint32_t _maxPacketCount;

    // tracking playing; the stupid AudioQueue property listener
    // doesn't notice if you actually pause playing.
    uint64_t _lastReturnedMs;	/* time an audio packet was last returned to us */
    float _dataRate;		/* estimated data rate */
    NSTimer *_spyTimer;		/* timer firing every second or so */
    NSTimer *_shutdownTimer;	/* used for delayed shutdown work */

    BOOL _lastUpcalledIsPlaying;
    BOOL _isPlaying;

    BOOL _upcalledShutdownState;
    SEL _stateCallbackSel;	/* who to notify, if anyone */
    id _stateCallbackObj;	/* ditto */

    SEL _songCallbackSel;	/* who to notify, if anyone */
    id _songCallbackObj;	/* ditto */

    /* buffers start off available, and get filled by an asynchronous
     * process as our MFANStreamPlayer_PacketsProc function is called by
     * the stream parser and sent to the AudioQueue player.
     *
     * They get played by the AudioQueue player, and then returned to
     * us by the AudioPlayer calling our MFANStreamPlayer_handleOutput
     * function.  Once returned, they wait in the available queue
     * until we have more data to stick in them, at which point we
     * fill them again and supply them back to the AudioQueue.
     *
     * The main edge condition is if the parser sends us data after
     * we've already sent all of our available packets to the
     * AudioQueue (availCount is 0). In this case, the parser thread
     * blocks until the AudioQueue finishes up a buffer.
     */
    AudioQueueBufferRef _buffersp[MFANStreamPlayer_nBuffers];
    uint32_t _availIx;
    uint32_t _availCount;
    BOOL _availEmptyWaiter;		/* parser is waiting for available buffers */
    BOOL _availFullWaiter;		/* someone's waiting for space to add a new buffer */

    BOOL _shutdown;
    BOOL _paused;
    BOOL _pthreadDone;

    NSThread *_readerThread;

    /* state to keep track of main player pthread */
    pthread_cond_t _aqCond;

    /* rather than have a huge local, this array stores the packet
     * descriptors for the audio buffer we're about to queue.  Its
     * contents are valid only for the duration of the incoming
     * packets callback.
     */
    AudioStreamPacketDescription *_packetsp;
}

int _songCount = 0;

/* we're getting callbacks from a C++ library, so the callbacks
 * aren't holding reference counts on the MFANStreamPlayer object.
 *
 * To get rid of an MFANStreamPlayer, we grab the global aqMutex, and
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
 * dropping a pointer to the MFANStreamPlayer.  To ensure that we will
 * free the structure properly in that case, we delay setting _refHeld
 * until play is called, even though the refCount > 0.  The play
 * function calls checkRefNL, which set refHeld if the refCount > 0
 * (which it will be).  This is probably a stupid feature, but c'est
 * la vie.
 */

/* one per system, i.e. static variables; we keep aqMutex static so
 * that we can reference it for cancellation tests even if our aqp has
 * been freed as part of a shutdown.
 */
static int _staticSetup=0;
static pthread_mutex_t _playerMutex;

/* this is called by the AudioQueue package when it finishes playing some music, telling
 * us to make available for filling with data a buffer we'd been using.
 *
 * This can be called on threads other than the main thread.
 */
void
MFANStreamPlayer_handleOutput( void *acontextp,
			       AudioQueueRef aqRefp,
			       AudioQueueBufferRef bufRefp)
{
    MFANStreamPlayer *aqp = (__bridge MFANStreamPlayer *) acontextp;

    /* push the buffer for reuse */
    pthread_mutex_lock(&_playerMutex);
    [aqp pushBuffer: bufRefp];
    aqp->_lastReturnedMs = osp_time_ms();
    pthread_mutex_unlock(&_playerMutex);
}

/* called on the main thread to create a new player */
- (MFANStreamPlayer *) initWithStream: (MFANAqStream *) stream
{
    self = [super init];
    if (self) {
	NSLog(@"- streamplayer init starts for %p", self);

	// Remember the stream
	_aqStream = stream;

	if ( !_staticSetup) {
	    pthread_mutex_init(&_playerMutex, NULL);
	    _staticSetup = 1;
	}

	pthread_cond_init(&_aqCond, NULL);

	_spyTimer = nil;
	_shutdownTimer = nil;

	_dataRate = 0.0;
	_lastReturnedMs = osp_time_ms();
	_lastUpcalledIsPlaying = NO;
	_upcalledShutdownState = NO;
	_isPlaying= NO;
	_stateCallbackObj = nil;

	_availIx = 0;
	_availCount = 0;
	_paused = NO;
	_shutdown = NO;
	_availEmptyWaiter = NO;
	_availFullWaiter = NO;
	_packetsp = NULL;
	_currentPlaying = MFANStreamPlayer_getUnknownString();
	_songCount++;

	_maxBufferSize = 0x4000;
	_maxPacketCount = 512;

        _packetsp = ((AudioStreamPacketDescription *)
		     malloc(_maxPacketCount *
			    sizeof(AudioStreamPacketDescription)));

	NSLog(@"- streamplayer init2 starts for %p cplaying=%p", self, _currentPlaying);

	_spyTimer = [NSTimer scheduledTimerWithTimeInterval: 1.0
			     target:self
			     selector:@selector(spyMonitor:)
			     userInfo:nil
			     repeats: YES];

	_pthreadDone = NO;
	_readerThread = [[NSThread alloc] initWithTarget: self
					       selector: @selector(playAsync:)
					       object: nil];
	[_readerThread start];
    }
    return self;
}

/* called on main thread to see if the music is actually playing, i.e. if buffers
 * of music are being returned to our free pool.
 */
- (void) spyMonitor: (id) junk
{
    uint64_t now;

    pthread_mutex_lock(&_playerMutex);

    now = osp_time_ms();
    if (now - _lastReturnedMs > 2500) {
	_isPlaying = NO;
    }
    else {
	_isPlaying = YES;
    }

#if 0
    if (now - _lastReturnedMs > 5000) {
	[self shutdown];

	/* and force new upcall, so player detects streamplayer has stopped; only do this once
	 * for a given streamplayer.
	 */
	if (!_upcalledShutdownState) {
	    _upcalledShutdownState = YES;
	    _lastUpcalledIsPlaying = !_isPlaying;
	}
    }
#endif

    if (_stateCallbackObj != nil && _lastUpcalledIsPlaying != _isPlaying) {
	_lastUpcalledIsPlaying = _isPlaying;
	pthread_mutex_unlock(&_playerMutex);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
	dispatch_async(dispatch_get_main_queue(), ^{
		[self->_stateCallbackObj performSelector: self->_stateCallbackSel
					      withObject: nil];
	    });
#pragma clang diagnostic pop
	return;
    }
    else {
	pthread_mutex_unlock(&_playerMutex);
    }
}

/* must be called with the aqLock held; drops lock temporarily;
 * Called from various threads.
 */
- (void) shutdown
{
    NSLog(@"- in shutdown audio for aqp=%p", self);

    /* check if we've started a shutdown procedure */
    pthread_mutex_lock(&_playerMutex);
    if (_shutdown) {
	NSLog(@"- shutdownAudio for stopped player %p", self);
	pthread_mutex_unlock(&_playerMutex);
	return;
    }

    /* if we're called on the wrong thread, move to the right one.  If we don't
     * do this, the NSTimer stuff doesn't work.
     */
    if (![NSThread isMainThread]) {
	NSLog(@" - shutdownAudio bouncing to main thread");
	dispatch_async(dispatch_get_main_queue(), ^{
		[self shutdown];
	    });
	pthread_mutex_unlock(&_playerMutex);
	return;
    }

    _shutdown = YES;

    // drop the lock in case something we do during shutdown triggers a synchronous
    // callback.
    pthread_mutex_unlock(&_playerMutex);

    /* we're shutting down the audio queue, thus it won't be paused afterwards */
    _paused = NO;

    /* stop the audio queue and dispose of it; can't set "immediate" to true,
     * since the AudioQueue system deadlocks if you do.
     */
    if (_audioQueue) {
	AudioQueueStop(_audioQueue, /* immediate */ 1);
	AudioQueueDispose(_audioQueue, /* immediate */ 0);
    }
    NSLog(@"just stopped AudioQueue %p", _audioQueue);

    // this, plus the shutdown flag beimg set, will cause the playAsync
    // thread's read to fail and the thread will exit.  The thread's
    // exiting will drop the thread's reference to the StreamPlayer
    [_streamReader close];

    if (_aqStream) {
	[_aqStream detachTarget];
    }

    /* leave _audioQueue pointer set in case we need it during parse call shutdown */
    _audioQueue = NULL;

    _shutdownTimer = [NSTimer scheduledTimerWithTimeInterval: 1.0
			      target:self
			      selector:@selector(shutdownPart2:)
			      userInfo:nil
			      repeats: NO];
    NSLog(@"- shutdown starts timer %p", self);
}

/* the timer itself should keep our MFANStreamPlayer allocated until it fires */
- (void) shutdownPart2: (id) junk
{
    int32_t i;

    pthread_mutex_lock(&_playerMutex);

    NSLog(@"- shutdown part2 starts=%p", self);

    /* free packets array; these are all recreated when PropertyProc is called when 
     * the stream is recreated.
     */
    if (_packetsp != NULL) {
	free(_packetsp);
	_packetsp = NULL;
    }

    /* free buffers */
    for(i=0;i<MFANStreamPlayer_nBuffers;i++) {
	if (_buffersp[i]) {
	    AudioQueueFreeBuffer(_audioQueue, _buffersp[i]);
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

    pthread_mutex_unlock(&_playerMutex);

    pthread_cond_broadcast(&_aqCond);	/* just in case */

    NSLog(@"- done with shutdown audio for aqp=%p", self);
}

/* called when someone presses pause */
- (void) pause
{
    pthread_mutex_lock(&_playerMutex);
    if (_shutdown) {
	pthread_mutex_unlock(&_playerMutex);
	return;
    }

    if (_audioQueue) {
	AudioQueuePause(_audioQueue);
    }
    _paused = YES;
    pthread_mutex_unlock(&_playerMutex);
}

/* called when someone presses resume */
- (void) resume
{
    pthread_mutex_lock(&_playerMutex);
    if (_shutdown) {
	pthread_mutex_unlock(&_playerMutex);
	return;
    }

    if (_audioQueue) {
	AudioQueueStart(_audioQueue, NULL);
    }
    _paused = NO;
    pthread_mutex_unlock(&_playerMutex);
}

- (void) setStateCallback: (id) callbackObj  sel: (SEL) callbackSel
{
    pthread_mutex_lock(&_playerMutex);
    _stateCallbackObj = callbackObj;
    _stateCallbackSel = callbackSel;
    pthread_mutex_unlock(&_playerMutex);
}

- (void) setSongCallback: (id) callbackObj  sel: (SEL) callbackSel
{
    pthread_mutex_lock(&_playerMutex);
    _songCallbackObj = callbackObj;
    _songCallbackSel = callbackSel;
    pthread_mutex_unlock(&_playerMutex);
}

- (BOOL) isPlaying
{
    /* note that the k...isRunning property doesn't notice
     * pause/resume, and so is useless.  We track output buffer handle
     * returns instead, using a timer.
     */
    return _isPlaying;
}

- (AudioQueueRef) setupAudioQueue {
    AudioQueueRef audioQueue;
    OSStatus osStatus;

    [_aqStream getDataFormat: &_dataFormat];
    osStatus = AudioQueueNewOutput ( &_dataFormat,
				     MFANStreamPlayer_handleOutput,
				     (__bridge void *) self,
				     /* runloop */ NULL,
				     /* ioop mode */ kCFRunLoopCommonModes,
				     /* flags */ 0,
				     &audioQueue);
    if (osStatus != 0) {
	NSError *error = [NSError errorWithDomain: NSOSStatusErrorDomain
					     code: osStatus
					 userInfo: nil];
	NSLog(@"! AudioQueue error %@", [error description]);
	return nil;
    }

    for(uint32_t i=0;i<MFANStreamPlayer_nBuffers;i++) {
	AudioQueueAllocateBuffer( audioQueue,
				  _maxBufferSize,
				  &_buffersp[i]);
	[self pushBuffer: _buffersp[i]];
    }

    /* turn up the volume; it is nice that this poorly documented
     * interface defaults to playing silently, so that if you forget
     * this step, you won't hear anything, or have a clue why.
     * Thanks, Apple.
     */
    Float32 gain = 1.0;
    osStatus = AudioQueueSetParameter ( audioQueue,
					kAudioQueueParam_Volume,
					gain);

    osStatus = AudioQueueStart( audioQueue, NULL);
    return audioQueue;
}

- (void) playAsync: (id) junk
{
    MFANAqStreamPacket *packet;
    AudioQueueBufferRef bufRefp = nil;
    uint32_t bytesCopied = 0;
    uint32_t packetsCopied = 0;
    OSStatus osStatus;

    while(!_shutdown) {
	if (_streamReader == nil) {
	    // Create a reader
	    _streamReader = [[MFANAqStreamReader alloc] initWithStream: _aqStream];
	}

	// if we have data to flush and no more data queued for us,
	// send it to the AudioQueue now, and then wait for more.
	if (![_streamReader hasData] && packetsCopied > 0) {
	    // flush pending data if we're going to block waiting for
	    // more data.  Note that once we have processed any
	    // packets, we should have created the AudioQueue.
	    osp_assert(_audioQueue != nil);
	    bufRefp->mAudioDataByteSize = bytesCopied;

	    NSLog(@"just queued %d bytes", bytesCopied);
	    osStatus = AudioQueueEnqueueBuffer ( _audioQueue,
						 bufRefp,
						 packetsCopied,
						 _packetsp);
	    if (osStatus != 0) {
		/* buffer didn't get queued so we have to avoid losing it */
		NSLog(@"! streamplayer enqueue failed, repushing");
		pthread_mutex_lock(&_playerMutex);
		[self pushBuffer: bufRefp];
		pthread_mutex_unlock(&_playerMutex);
	    }
	    bufRefp = NULL;

	    pthread_mutex_lock(&_playerMutex);
	    bufRefp = [self popBuffer];
	    pthread_mutex_unlock(&_playerMutex);
	    bytesCopied = 0;
	    packetsCopied = 0;
	}

	packet = [_streamReader read];
	if (packet == nil) {
	    // someone has closed the stream; had there been no data
	    // available yet, the read call would have simply blocked.
	    break;
	}

	// once we have a packet, we can find out the stream type,
	// which is required for creating the queue.
	if (_audioQueue == nil) {
	    _audioQueue = [self setupAudioQueue];
	}

	// make sure we have a buffer to hold the data we're reading.
	// this must be done after creating the audio queue, or we
	// won't have setup the buffers.
	if (bufRefp == nil) {
	    pthread_mutex_lock(&_playerMutex);
	    bufRefp = [self popBuffer];
	    pthread_mutex_unlock(&_playerMutex);
	    bytesCopied = 0;
	    packetsCopied = 0;
	}

	// Now that we have a packet, a queue and a buffer, do we have
	// room in the buffer for this packet of data?
	uint64_t packetSize = [packet getLength];
	if (bytesCopied + packetSize <= _maxBufferSize &&
	    packetsCopied + 1 <= _maxPacketCount) {

	    // and do the copy: copy the data and the descriptor, and
	    // then adjust the mStartOffset field to point to the
	    // record's position in the new buffer.
	    memcpy( (char *)bufRefp->mAudioData+bytesCopied,
		    [packet getData],
		    (size_t) packetSize);

	    [packet getDescr: &_packetsp[packetsCopied]];
	    _packetsp[packetsCopied].mStartOffset = bytesCopied;

	    bytesCopied += packetSize;
	    packetsCopied++;
	    continue;
	}

	// packet doesn't fit, so queue the buffer
	bufRefp->mAudioDataByteSize = bytesCopied;

	osStatus = AudioQueueEnqueueBuffer ( _audioQueue,
					     bufRefp,
					     packetsCopied,
					     _packetsp);
	NSLog(@"enqueued %d bytes %d packets", bytesCopied, packetsCopied);
	if (osStatus != 0) {
	    /* buffer didn't get queued so we have to avoid losing it */
	    NSLog(@"! streamplayer enqueue failed, repushing");
	    pthread_mutex_lock(&_playerMutex);
	    [self pushBuffer: bufRefp];
	    pthread_mutex_unlock(&_playerMutex);
	}
	bytesCopied = 0;
	packetsCopied = 0;
	bufRefp = NULL;
    }

    NSLog(@"StreamPlayer async thread exited");
    if (!_shutdown) {
	NSLog(@"StreamPlayer thread triggers shutdown");
	// Thread exited but no one did a shutdown, so do it now
	[self shutdown];
    }

    _pthreadDone = YES;
    pthread_exit(NULL);
}

/* called with _playerMutex held, to add a buffer to the free list of buffers
 * More can be sent to us than we have room for, in which case we block
 * until room has freed up.
 */
- (void) pushBuffer: (AudioQueueBufferRef) item
{
    uint32_t ix;

    while (_availCount >= MFANStreamPlayer_nBuffers) {
	_availFullWaiter = YES;
	pthread_cond_wait(&_aqCond, &_playerMutex);
    }

    ix = _availIx+_availCount;
    if (ix >= MFANStreamPlayer_nBuffers)
	ix -= MFANStreamPlayer_nBuffers;
    _buffersp[ix] = item;

    _availCount++;
    if (_availEmptyWaiter) {
	_availEmptyWaiter = NO;
	pthread_cond_broadcast(&_aqCond);
    }
}

/* called with _playerMutex held */
- (AudioQueueBufferRef) popBuffer
{
    AudioQueueBufferRef item;

    while (_availCount <= 0) {
	if (_shutdown) {
	    return NULL;
	}
	_availEmptyWaiter = YES;
	pthread_cond_wait(&_aqCond, &_playerMutex);
    }
    _availCount--;
    item = _buffersp[_availIx];
    if (++_availIx >= MFANStreamPlayer_nBuffers)
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

- (BOOL) isShutdown
{
    return _shutdown;
}

- (void) dealloc {
    NSLog(@"- dealloc of streamplayer %p", self);
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
    if (_aqStream) {
	return [_aqStream getUrl];
    } else {
	return @"[None]";
    }
}

- (float) dataRate
{
    return _dataRate;
}

@end
