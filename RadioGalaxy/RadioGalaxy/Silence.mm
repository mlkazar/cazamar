#import <AudioToolbox/AudioToolbox.h>

#import "MFANCGUtil.h"
#import "Silence.h"

#include "osp.h"

@implementation Silence {
    NSData *_silentData;
    AVAudioPlayer *_silentPlayer;
    BOOL _isPlaying;
}
- (Silence *) init {
    NSError *setError;

    self = [super init];
    if (self != nil) {
	_silentData = silentData(10);
	_isPlaying = false;

	_silentPlayer = [[AVAudioPlayer alloc] initWithData: _silentData error:&setError];
	_silentPlayer.volume = 0.0;
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
}

- (void) stop {
    NSLog(@"=1= stopping bkg, prev state isPlaying=%d", _isPlaying);
    [_silentPlayer stop];
    _isPlaying = false;
}

@end

