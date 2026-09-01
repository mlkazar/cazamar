//
//  ViewController.m
//  RadioStar
//
//  Created by Michael Kazar on 11/25/25.
//

#import <BackgroundTasks/BackgroundTasks.h>
#import "Settings.h"
#import "Silence.h"
#import "TopView.h"
#import "ViewController.h"

// This class depends upon two classes by name:
//
// TopView is the top level view that gets plugged in.  It really
// seems that the view plugged into the ViewController will always
// have the full screen dimensions, and 

@implementation ViewController {
    NSMutableArray<UIView<TopViewInt> *> *_viewStack;	// of UIView objects
    float _topMargin;
    float _bottomMargin;
    UIColor *_backgroundColor;
    CGRect _activeFrame;
    UIView<TopViewInt> *_activeView;
    Settings *_settings;	// convenient place to find app-specific settings

    // stuff for background management
    Silence *_silence;
    BOOL _isBackground;
    BOOL _isInterrupted;

    // for NowPlayingCenter
    NSMutableDictionary *_nowPlayingInfo;
    UIImage *_inputImage;
    UIImage *_convertedImage;
    int32_t _songIndex;
}

// The view in self.view is a whole screen view painted black.  It
// will be given child views with top and bottom margins, that will
// typically be white.  The _viewStack array is a stack of active
// views that can be restored as popup views terminate; it includes
// the activeView as the last element.

- (void)viewDidLoad {
    CGRect rect = self.view.frame;

    NSLog(@"In viewDidLoad");
    [super viewDidLoad];

    // setup views and viewable areas
    _viewStack = [[NSMutableArray alloc] init];

    _topMargin = 50;
    _bottomMargin = 50;

    self.view = [[UIView alloc] initWithFrame: rect];
    self.view.backgroundColor = [UIColor blackColor];

    _songIndex = 2000;

    _activeFrame = rect;
    _activeFrame.origin.y += _topMargin;
    _activeFrame.size.height -= _topMargin + _bottomMargin;
    _activeView = [[TopView alloc] initWithFrame: _activeFrame
					ViewCont: self];

    [_viewStack addObject: _activeView];
    [self.view addSubview: _activeView];

    _silence = [[Silence alloc] init];
    _isBackground = false;
    _isInterrupted = false;

    [[UIApplication sharedApplication] beginReceivingRemoteControlEvents];

    [[NSNotificationCenter defaultCenter] addObserver: self
					     selector: @selector(audioInterruption:)
						 name: AVAudioSessionInterruptionNotification
					       object: nil];

    [self registerBackground];
}

+ (void) splitLabel: (NSString *) label
	      group: (NSString **) group
	       song: (NSString **) song
	      album: (NSString **) album {

    NSArray<NSString *> *parsed = [label componentsSeparatedByString: @"-"];
    uint64_t parsedCount = [parsed count];
    if (parsedCount == 0) {
	*group = @"Unknown";
	*song = @"Unknown";
	*album = @"Unknown";
    } else if (parsedCount == 1) {
	*group = @"Unknown";
	*song = [parsed[0] stringByTrimmingCharactersInSet:
			   NSCharacterSet.whitespaceCharacterSet];
	*album = @"";

    } else if (parsedCount == 2) {
	*group = [parsed[0] stringByTrimmingCharactersInSet:
			   NSCharacterSet.whitespaceCharacterSet];
	*song = [parsed[1] stringByTrimmingCharactersInSet:
			   NSCharacterSet.whitespaceCharacterSet];
	*album = @"";

    } else {
	*group = [parsed[0] stringByTrimmingCharactersInSet:
			    NSCharacterSet.whitespaceCharacterSet];
	*song = [parsed[1] stringByTrimmingCharactersInSet:
			   NSCharacterSet.whitespaceCharacterSet];
	*album = [parsed[2] stringByTrimmingCharactersInSet:
			    NSCharacterSet.whitespaceCharacterSet];
    }
}

- (MPMediaItemArtwork *) getArtworkForImage: (UIImage *) image {
    MPMediaItemArtwork *artwork = [[MPMediaItemArtwork alloc]
				      initWithBoundsSize: image.size
					  requestHandler: ^UIImage * _Nonnull(CGSize rect) {
	    return image;
	}];
    return artwork;
}

- (void) updateNowPlayingCenter: (NSString *) label
		      baseImage: (UIImage *) image
		    currentTime: (float) currentTime
		       duration: (float) durationTime
		      songIndex: (int32_t) songIndex {
    NSMutableDictionary *info = [[NSMutableDictionary alloc] init];
    NSString *songTitle;
    NSString *songArtist;
    NSString *songAlbum;

    // Always fills in all fields.
    [ViewController splitLabel: label
			 group: &songArtist
			  song: &songTitle
			 album: &songAlbum];

    [info setObject: [NSNumber numberWithDouble: 1.0]
	     forKey: MPNowPlayingInfoPropertyPlaybackRate];

    [info setObject: [NSNumber numberWithFloat: currentTime]
	     forKey: MPNowPlayingInfoPropertyElapsedPlaybackTime];

    [info setObject: [NSNumber numberWithFloat: durationTime]
	     forKey: MPMediaItemPropertyPlaybackDuration];

    if (songIndex < 0)
	songIndex = _songIndex++;

    [info setObject: [NSNumber numberWithUnsignedInt: songIndex]
	     forKey: MPNowPlayingInfoPropertyPlaybackQueueIndex];

    // never want to have songIndex >= queueCount
    [info setObject: [NSNumber numberWithUnsignedInt: (int) 8000]
	     forKey: MPNowPlayingInfoPropertyPlaybackQueueCount];

    [info setObject: songTitle forKey: MPMediaItemPropertyTitle];
    [info setObject: songArtist forKey: MPMediaItemPropertyArtist];
    [info setObject: songAlbum forKey: MPMediaItemPropertyAlbumTitle];

    MPMediaItemArtwork *artWork = [self getArtworkForImage: image];
    [info setObject: artWork forKey: MPMediaItemPropertyArtwork];

    _nowPlayingInfo = info;
    [[MPNowPlayingInfoCenter defaultCenter] setNowPlayingInfo: info];

}

- (void) resumePlayerAfterInterruption {
    _isInterrupted = true;
    [self setupAudioSession: true];
    [_silence start];
}

- (BOOL) isPlaying {
    return [self applySelector: @selector(tvIsPlaying)];
}

- (void) audioInterruption: (NSNotification *) notification {
    NSDictionary *userInfo = [notification userInfo];
    NSNumber *intKey;
    NSNumber *optKey;
    long intType;

    intKey = (NSNumber *) userInfo[AVAudioSessionInterruptionTypeKey];
    optKey = (NSNumber *) userInfo[AVAudioSessionInterruptionOptionKey];

    intType = [intKey longValue];
    if (intType == AVAudioSessionInterruptionTypeEnded) {
	NSLog(@"=1= audio interruption ended");
	if ([optKey longValue] & AVAudioSessionInterruptionOptionShouldResume) {
	    NSLog(@"=1= resuming audio player");
	    // also calls checkUpcallState
	    [self applySelector: @selector(tvResume)];
	}
    }
    else if (intType == AVAudioSessionInterruptionTypeBegan) {
	NSLog(@"=1= audio interruption began");
	// also calls checkUpcallState
	_isInterrupted = true;
	[self applySelector: @selector(tvPause)];
    }
    else {
	NSLog(@"=1= audio interruption unknown type %ld", intType);
    }

    // adjust session state.
    [self processBackgroundState];
}

// Should eventually move to BGContinuedProcessingTask, but that's
// only supported on very new iOS versions.
- (void) registerBackground {
    [[BGTaskScheduler sharedScheduler]
	registerForTaskWithIdentifier: @"com.Cazamar.RadioGalaxy.background"
			   usingQueue: dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0)
			launchHandler: ^(BGTask *_Nonnull task) {
	    [self runBackgroundTask: (BGProcessingTask *) task];
	}];
}

- (void) runBackgroundTask: (BGProcessingTask *) task {
    NSLog(@"=1= running BACKGROUND task");
    [NSThread sleepForTimeInterval: 1.0];
}

- (void) pushTopView: (UIView<TopViewInt> *) view {
    // notify old view that it isn't active any more and remove it
    // from view chain.
    [_activeView tvDeactivate];
    [_activeView removeFromSuperview];

    _activeView = view;
    [_viewStack addObject: view];

    // notify new view it is active, and save it in _activeView.
    [self.view addSubview: view];
    [view tvActivate];
}

// return true unless any view in stack says no.  Can't use
// applySelector because it quits on first responding selector.
- (bool) ok2Quit {
    UIView<TopViewInt> *view;
    bool result = true;

    if (!_settings.exitWhenIdle)
	return false;

    for(view in _viewStack) {
	if ([view respondsToSelector: @selector(tvOk2Quit)]) {
	    bool (*method)(id, SEL, id) =
		(bool (*)(id, SEL, id))[view methodForSelector: @selector(tvOk2Quit)];
	    result = method(view, @selector(tvOk2Quit), nil);
	    if (!result)
		break;
	}
    }

    return result;
}

- (void) popTopView {
    UIView<TopViewInt> *prevView;

    // deactivate current view and remove from chain
    [_activeView tvDeactivate];
    [_activeView removeFromSuperview];

    // find previous view to reactivate, and put it in activeView
    [_viewStack removeLastObject];
    prevView = [_viewStack lastObject];
    _activeView = prevView;

    // Notify new active view.
    [self.view addSubview: prevView];
    [prevView tvActivate];
}

- (void) enterBackground {
    _isBackground = true;
    [self processBackgroundState];
}

- (void) leaveBackground {
    _isBackground = false;
    [self processBackgroundState];
}

// There are two things to know about receiving events from other applications
// The lock screen and carplay controls show up as remote controlevents, delivered
// to remoteCotrolReceivedWithEvent below in the active viewcontroller.
//
// The notification center also must be integrated with, in order to stop playing when
// another audio source takes over, like when a phone call arrives.
- (bool) applySelector: (SEL) sel {
    int64_t count = [_viewStack count];
    int64_t ix;
    UIView<TopViewInt> *view;
    bool rval = false;

    for(ix = count-1; ix >= 0; ix--) {
	view = _viewStack[ix];
	// skip stack elements that don't reespond.
	if ([view respondsToSelector: sel]) {
	    bool (*method)(id, SEL, id) = (bool (*)(id, SEL, id))[view methodForSelector: sel];
	    rval = method(view, sel, nil);
	    break;
	}
    }

    return rval;
}

// This is responsible for updating the AudioSession between mix and
// don't mix, and setting up the Silence player to keep Apple happy
// when nothing else is playing.  It also contains a hack to handle
// the case where we don't get an audio interruption 'end'
// notification.
//
// Generally, we want the silence player running if we aren't playing
// music, so that *something* is playing at all times, to avoid our
// app being killed for lack of using the audio channels.
//
// Also, our app won't get killed if it is in the foreground (no lock
// screen).
//
// Note that the lock screen / car play controls only work with mix
// false.  If mix is false, however, a route change stops all audio,
// and we eventually get killed, so we want to start a mix == true
// player when we get interrupted with a route change.
//
// One problem we encounter is if we switch to a dead station, it
// looks like we're not playing any more, we start the silence player
// in mix mode and lose access to car play controls.  So, if we're not
// playing but didn't hit pause, we leave the controls in place and
// hope the jammed up player will continue before we get killed.
//
// In general, we can have mix set, in which case we can keep playing
// music or keep the app running with silence, but car play controls
// don't work.
- (void) processBackgroundState {
    BOOL isPlaying = [self isPlaying];
    NSLog(@"=1= PBS isBackground=%d isPlaying=%d interrupted=%d",
	  _isBackground, isPlaying, _isInterrupted);
    if (isPlaying) {
	// player has been restarted.  We don't always get interruption ended events,
	// so in this case we simulate one.
	_isInterrupted = false;
    }
    if (_isBackground) {
	// We want a stalled player (not playing but not paused) to
	// keep the controls around (use mix == false) .  Such a
	// player will timeout in 15 seconds or so, so we'll get a
	// chance to start the silence player going before we get
	// killed (hopefully).
	if (!isPlaying) {
	    // if we were interrupted by another audio app, we have to use
	    // mix == true so that the silent player keeps running.
	    // to keep the app alive.
	    //
	    // Otherwise, we want to keep the non-mixing session, so that
	    // the remote controls can keep working.
	    [self setupAudioSession: _isInterrupted];
	    [_silence start];
	} else {
	    // playing, so we don't need more things playing in order
	    // to keep our process around.  Or we're not playing
	    // because of a stall, which hopefully won't last long.
	    [_silence stop];
	    [self setupAudioSession: false];
	}

	// see if we should quit the app because of inactivity
	if ([self ok2Quit])
	    exit(0);
    } else {
	// foreground, don't have to worry about being killed
	[_silence stop];
	_isInterrupted = false;
	[self setupAudioSession: false];
    }
}

- (void) setupAudioSession: (BOOL) mix {
    NSError *setError;
    AVAudioSession *audioSession = [AVAudioSession sharedInstance];
    if (mix) {
	// can't setup callbacks, but setup the session
	NSLog(@"=1= setupAudioSession mix");
	[audioSession setCategory: AVAudioSessionCategoryPlayback
		      withOptions: AVAudioSessionCategoryOptionMixWithOthers
			    error: &setError];
    } else {
	NSLog(@"=1= setupAudioSession playback");
        [audioSession setCategory: AVAudioSessionCategoryPlayback
		      withOptions: 0
			    error: &setError];
    }

    [audioSession setActive: true error: &setError];

    // make sure we keep getting notifications for the new session.
    // [self setupNotifications];
}


- (void)remoteControlReceivedWithEvent:(UIEvent *)receivedEvent {
    if (receivedEvent.type == UIEventTypeRemoteControl) {
        switch (receivedEvent.subtype) {
            case UIEventSubtypeRemoteControlPlay:
		[self applySelector: @selector(tvResume)];
		break;

            case UIEventSubtypeRemoteControlPause:
		[self applySelector: @selector(tvPause)];
		break;

            case UIEventSubtypeRemoteControlTogglePlayPause:
		NSLog(@"=1= SignView play/pause %ld", (long) receivedEvent.subtype);
		[self applySelector: @selector(tvPlayPauseSong)];
                break;

            case UIEventSubtypeRemoteControlPreviousTrack:
		[self applySelector: @selector(tvPrevSong)];
                break;

            case UIEventSubtypeRemoteControlNextTrack:
		[self applySelector: @selector(tvNextSong)];
                break;

            default:
                NSLog(@"!RMT mystery pressed %d", (int) receivedEvent.subtype);
                break;
        }

	[self processBackgroundState];

        [[UIApplication sharedApplication] beginReceivingRemoteControlEvents];
    }

}

@end
