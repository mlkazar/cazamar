#import <AudioToolbox/AudioToolbox.h>

#import "MFANCGUtil.h"
#import "Silence.h"

#include "osp.h"

@implementation Silence {
    NSData *_silentData;
    AVAudioPlayer *_silentPlayer;
    BOOL _isPlaying;
    NSTimer *_timer;
}
- (Silence *) init {
    NSError *setError;

    self = [super init];
    if (self != nil) {
	_silentData = silentData(1800);
	_isPlaying = false;

	_silentPlayer = [[AVAudioPlayer alloc] initWithData: _silentData error:&setError];
	_silentPlayer.volume = 0.2;
	if ([setError code] != 0)
	    NSLog(@"! SilentPlayer init failed %d", (int) [setError code]);
	[_silentPlayer setNumberOfLoops: -1];
    }

    return self;
}

- (void) start {
    bool started;
    NSLog(@"=1= starting bkg, prev state isPlaying=%d", _isPlaying);

    started = [_silentPlayer play];

    _isPlaying = true;

    _timer = [NSTimer scheduledTimerWithTimeInterval: 10.0
					      target: self
					    selector: @selector(checkRunning:)
					    userInfo: nil
					     repeats: YES];

}

- (void) stop {
    NSLog(@"=1= stopping bkg, prev state isPlaying=%d", _isPlaying);
    if (_timer != nil) {
	[_timer invalidate];
	_timer = nil;
    }
    [_silentPlayer stop];
    _isPlaying = false;
}

- (void) checkRunning: (id) junk {
    // if it should be playing and there's player
    if (_isPlaying) {
	if ( _silentPlayer != nil && !_silentPlayer.isPlaying) {
	    [_silentPlayer play];
	    NSLog(@"=1= timer restarted silentPlayer");
	}
    }
}

@end
