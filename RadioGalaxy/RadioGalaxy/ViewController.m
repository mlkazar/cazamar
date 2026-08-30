//
//  ViewController.m
//  RadioStar
//
//  Created by Michael Kazar on 11/25/25.
//

#import <BackgroundTasks/BackgroundTasks.h>
#import "ViewController.h"
#import "TopView.h"

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
    UIView<AudioInt> *_remoteReceiver;
    NSObject *_settings;	// convenient place to find app-specific settings
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

    _activeFrame = rect;
    _activeFrame.origin.y += _topMargin;
    _activeFrame.size.height -= _topMargin + _bottomMargin;
    _activeView = [[TopView alloc] initWithFrame: _activeFrame
					ViewCont: self];

    [_viewStack addObject: _activeView];
    [self.view addSubview: _activeView];

    [self registerBackground];
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
    [_activeView deactivateTopView];
    [_activeView removeFromSuperview];

    _activeView = view;
    [_viewStack addObject: view];

    // notify new view it is active, and save it in _activeView.
    [self.view addSubview: view];
    [view activateTopView];
}

// return true unless any view in stack says no.
- (bool) ok2Quit {
    UIView<TopViewInt> *view;
    bool result = true;

    for(view in _viewStack) {
	if ([view respondsToSelector: @selector(ok2Quit)]) {
	    result = [view performSelector:@selector(ok2Quit)];
	    if (!result)
		break;
	}
    }

    return result;
}

- (void) popTopView {
    UIView<TopViewInt> *prevView;

    // deactivate current view and remove from chain
    [_activeView deactivateTopView];
    [_activeView removeFromSuperview];

    // find previous view to reactivate, and put it in activeView
    [_viewStack removeLastObject];
    prevView = [_viewStack lastObject];
    _activeView = prevView;

    // Notify new active view.
    [self.view addSubview: prevView];
    [prevView activateTopView];
}

- (void) enterBackground {
    if (_remoteReceiver != nil) {
	// this will typically call into SignView in the RadioStar
	// app.  if 'mix' is true, remote control and lockscreen won't
	// work to control this.  Note sure if we have to do the
	// setupAudioSession again.
	if (_remoteReceiver != nil) {
	    [_remoteReceiver enterBackground];
	}
    }
}

- (void) leaveBackground {
    if (_remoteReceiver != nil) {
	[_remoteReceiver leaveBackground];
    }
}

// There are two things to know about receiving events from other applications
// The lock screen and carplay controls show up as remote controlevents, delivered
// to remoteCotrolReceivedWithEvent below in the active viewcontroller.
//
// The notification center also must be integrated with, in order to stop playing when
// another audio source takes over, like when a phone call arrives.

- (void) setRemoteReceiver: (UIView<AudioInt> *) remoteReceiver {
    _remoteReceiver = remoteReceiver;
    [[UIApplication sharedApplication] beginReceivingRemoteControlEvents];
}

- (void)remoteControlReceivedWithEvent:(UIEvent *)receivedEvent {
    if (_remoteReceiver != nil) {
	[_remoteReceiver remoteControlReceivedWithEvent: receivedEvent];
    }
}

@end
