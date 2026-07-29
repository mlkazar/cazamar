#import <AudioToolbox/AudioToolbox.h>

#import "MFANCGUtil.h"
#import "Silence.h"

#include "osp.h"

@implementation Silence {
    NSData *_silentData;
    AVAudioPlayer *_silentPlayer;
    BOOL _isPlaying;
    NSTimer *_timer;
    uint32_t _nloops;
    uint32_t _loopTime;
    uint64_t _lastStartSecs;
}
- (Silence *) init {
    NSError *setError;

    self = [super init];
    if (self != nil) {
	_nloops = 2;
	_loopTime = 10;
	_lastStartSecs = 0;
	_timer = nil;

	_silentData = silentData(_loopTime);
	_isPlaying = false;

	_silentPlayer = [[AVAudioPlayer alloc] initWithData: _silentData error:&setError];
	_silentPlayer.volume = 0.5;
	_silentPlayer.delegate = self;
	if ([setError code] != 0)
	    NSLog(@"! SilentPlayer init failed %d", (int) [setError code]);
	[_silentPlayer setNumberOfLoops: _nloops];
    }

    return self;
}

- (void) audioPlayerDidFinishPlaying: (AVAudioPlayer *) player successfully: (BOOL) flag {
    NSLog(@"=1= silent player done isPlaying=%d", _isPlaying);

    if (_isPlaying) {
	[self setupAudioSession];	// setup for mixing audio

	[_silentPlayer setNumberOfLoops: _nloops];
	[_silentPlayer play];		// resume playing
	_lastStartSecs = osp_time_sec();
	NSLog(@"=1= silent player restarted after finish playing callback");
    }
}

- (void) setupAudioSession {
    NSError *setError;
    AVAudioSession *audioSession = [AVAudioSession sharedInstance];

    // can't setup callbacks, but setup the session
    NSLog(@"=1= setupAudioSession silence mix");
    [audioSession setCategory: AVAudioSessionCategoryPlayback
		  withOptions: AVAudioSessionCategoryOptionMixWithOthers
			error: &setError];

    [audioSession setActive: true error: &setError];
}

- (void) start {
    bool started;

    NSLog(@"=1= starting bkg, prev state isPlaying=%d", _isPlaying);

    _isPlaying = true;
    [self setupAudioSession];
    started = [_silentPlayer play];
    if (started) {
	_lastStartSecs = osp_time_sec();
    } else {
	NSLog(@"=1= start failed to start silent player");
    }

    if (_timer) {
	[_timer invalidate];
    }

    _timer = [NSTimer scheduledTimerWithTimeInterval: _loopTime
					      target: self
					    selector: @selector(checkRunning:)
					    userInfo: nil
					     repeats: YES];
}

- (void) stop {
    NSLog(@"=1= stopping bkg, prev state isPlaying=%d", _isPlaying);
    _isPlaying = false;
    if (_timer != nil) {
	[_timer invalidate];
	_timer = nil;
    }
    [_silentPlayer stop];
}

- (void) checkRunning: (id) junk {
    uint64_t now = osp_time_sec();
    // if it should be playing and there's player
    NSLog(@"=1= bkg checkrunning");
    if (_isPlaying) {
	NSLog(@"=1= silence checkrunning isPlaying=%d now=%llu lastStart=%llu",
	      _silentPlayer.isPlaying, now, _lastStartSecs);
	if ( _silentPlayer != nil && (!_silentPlayer.isPlaying ||
				      (_lastStartSecs + _nloops * _loopTime + 6 < now))) {
	    [self setupAudioSession];
	    [_silentPlayer play];
	    _lastStartSecs = now;
	    NSLog(@"=1= silence checkrunning on timer restarted silentPlayer");
	} else {
	    NSLog(@"=1= checkrunning says things are fine");
	}
    }
}

@end
