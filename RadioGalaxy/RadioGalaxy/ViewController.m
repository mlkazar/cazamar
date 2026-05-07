//
//  ViewController.m
//  RadioStar
//
//  Created by Michael Kazar on 11/25/25.
//

#import "ViewController.h"
#import "TopView.h"

// This class depends upon two classes by name:
//
// TopView is the top level view that gets plugged in.  It really
// seems that the view plugged into the ViewController will always
// have the full screen dimensions, and 

@implementation ViewController {
    NSMutableArray<UIView<TopViewInt> *> *_oldViews;	// of UIView objects
    float _topMargin;
    float _bottomMargin;
    UIColor *_backgroundColor;
    CGRect _activeFrame;
    UIView<TopViewInt> *_activeView;
    UIView<AudioInt> *_remoteReceiver;
}

// The view in self.view is a whole screen view painted black.  It
// will be given child views with top and bottom margins, that will
// typically be white.  The _oldViews array is a stack of previously
// active views that can be restored as popup views terminate.

- (void)viewDidLoad {
    CGRect rect = self.view.frame;

    NSLog(@"In viewDidLoad");
    [super viewDidLoad];

    // setup views and viewable areas
    _oldViews = [[NSMutableArray alloc] init];

    _topMargin = 50;
    _bottomMargin = 50;

    self.view = [[UIView alloc] initWithFrame: rect];
    self.view.backgroundColor = [UIColor blackColor];

    _activeFrame = rect;
    _activeFrame.origin.y += _topMargin;
    _activeFrame.size.height -= _topMargin + _bottomMargin;
    _activeView = [[TopView alloc] initWithFrame: _activeFrame
					ViewCont: self];

    [self.view addSubview: _activeView];
}

- (void) pushTopView: (UIView<TopViewInt> *) view {
    // notify old view that it isn't active any more and remove it
    // from view chain.
    [_activeView deactivateTopView];
    [_oldViews addObject: _activeView];
    [_activeView removeFromSuperview];

    // notify new view it is active, and save it in _activeView.
    [self.view addSubview: view];
    [view activateTopView];
    _activeView = view;
}

- (void) popTopView {
    UIView<TopViewInt> *prevView;

    // deactivate current view and remove from chain
    [_activeView removeFromSuperview];
    [_activeView deactivateTopView];

    // find previous view to reactivate
    prevView = [_oldViews lastObject];
    [_oldViews removeLastObject];

    // remember it activeView and put it in the view chain, and
    // then notify it.
    _activeView = prevView;
    [self.view addSubview: _activeView];
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
