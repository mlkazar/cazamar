//
//  MFANViewController.m
//  MusicFan
//
//  Created by Michael Kazar on 4/22/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANViewController.h"
#import "MFANIndicator.h"
#import "MFANSetList.h"
#import "MFANTopHelp.h"
#import "MFANTopSettings.h"
#import "MFANRadioConsole.h"
#import "MFANTopList.h"
#import "MFANTopHistory.h"
#import "MFANPlayerView.h"
#import "MFANTopUpnp.h"
#import "MFANTopMenu.h"
#import "MFANTopApp.h"
#import "MFANTopDoc.h"

@interface MFANViewController ()
@end

@implementation MFANViewController {
    int _level;
    MFANIndicator *_indicator;
    MFANTopLevel *_topLevel;
    MFANTopEdit *_topEdit[MFANChannelTypeCount];
    MFANTopHelp *_topHelp;
    MFANTopHistory *_topHistory;
    MFANTopMenu *_topMenu;
    MFANTopList *_topList[MFANChannelTypeCount];
    MFANTopSettings *_topSettings;
    MFANTopUpnp *_topUpnp;
    MFANTopDoc *_topDoc;
    MFANPlayerView *_myPlayerView;
    UIView<MFANTopApp> *_activeView;
    MFANComm *_comm;
    MFANChannelType _channelType;
}

- (void) setChannelType: (MFANChannelType) channelType
{
    NSLog(@"setting channelType=%d", (int) channelType);
    _channelType = channelType;
}

- (UIView<MFANTopApp> *) getTopAppByName: (NSString *) name
{
    if ([name isEqualToString: @"main"])
	return _topLevel;
    else if ([name isEqualToString: @"edit"]) {
	return _topEdit[_channelType];
    }
    else if ([name isEqualToString: @"history"])
	return _topHistory;
    else if ([name isEqualToString: @"help"])
	return _topHelp;
    else if ([name isEqualToString: @"settings"])
	return _topSettings;
    else if ([name isEqualToString: @"list"])
	return _topList[_channelType];
    else if ([name isEqualToString: @"upnp"])
	return _topUpnp;
    else if ([name isEqualToString: @"menu"])
	return _topMenu;
    else if ([name isEqualToString: @"doc"])
	return _topDoc;
    else {
	NSLog(@"!BAD NAME TO getTopAppByName");
	return nil;
    }
}

- (void) switchToAppByName: (NSString *)name
{
    UIView<MFANTopApp> *app;

    app = [self getTopAppByName: name];
    if (app != nil) {
	[self makeActive: app];
    }
}

- (MFANTopSettings *) getSettings
{
    return _topSettings;
}

- (BOOL) shouldAutorotate
{
    return NO;
}

- (void) loadView
{
    CGRect frame = [[UIScreen mainScreen] bounds];

    [[UIApplication sharedApplication] setIdleTimerDisabled:YES];

    _indicator = [[MFANIndicator alloc] initWithFrame: frame];

    [self setView: _indicator];

    [NSThread detachNewThreadSelector:@selector(doSlowInit:)
	      toTarget: self
	      withObject: nil];
}

- (void) doSlowInit: (id) parm
{
    [MFANSetList doSetup: _indicator force: NO];

    [_indicator setDone];
    _indicator = nil;

    /* now continue execution in the main thread */
    [self performSelectorOnMainThread: @selector(loadViewPart2:)
	  withObject:nil
	  waitUntilDone:NO];
}

- (BOOL) canBecomeFirstResponder
{
    return YES;
}

- (void) loadViewPart2: (id) object
{
    CGRect frame = [[UIScreen mainScreen] bounds];
    MFANRadioConsole *radioConsole;

    /* use these three to allow us to get decent screen shots with any device; also modify
     * MFANTopSettings.h to turn off artwork.
     *
     * With sumopaint, first adjust canvas size to prune to right size, then resize image
     * to decent size, then select all and 'cut' this image.  Then set canvas to final
     * size and paste image.  Image appears in new canvas.  Create 2 more layers, one for
     * gray layer (CFCFCF) and one for black text.
     */
    int forceIPhone4 = 0;	/* 3.5 inch */
    int forceIPhone5S = 0;	/* 4 inch */
    int forceIPad = 0;		/* iPad */
    int forceIPhone65 = 0;	/* iPhone 6.5 " */
    /* none of the above is 4.7 inch / iPhone 6 */

    CGFloat physRatio = frame.size.height / frame.size.width;
    CGFloat targetRatio;
    CGFloat newWidth;
    CGFloat newHeight;
    int i;

    _channelType = MFANChannelMusic;	/* have a default */

    if (forceIPhone4) {
	targetRatio = 960.0/640.0;	/* 1.5 */
    }
    else if (forceIPhone5S) {
	targetRatio = 1136.0/640.0;	/* 1.775 */
    }
    else if (forceIPad) {
	/* or 2732/2048 for IPad pro */
	targetRatio = 2048.0/1536.0;	/* about 1.333 */
    }
    else if (forceIPhone65) {
	targetRatio = 2688.0/1242.0;	/* about 2.164 */
    }
    else {
	/* native ratio is 1334.0/750.0 = 1.775 */
	targetRatio = 0.0;		/* use basic screen */
    }
    /* note that iPadPro 3 is 2732 x 2048, which is the same ratio as iPad */

    if (targetRatio != 0.0) {
	if (targetRatio > physRatio) {
	    /* trying to simulate a taller phone, so shrink the width */
	    newWidth = frame.size.width * physRatio / targetRatio;
	    frame.origin.x = (frame.size.width - newWidth) / 2;
	    frame.size.width = newWidth;
	}
	else {
	    /* trying to simulate a wider phone, so shrink the height */
	    newHeight = frame.size.height * targetRatio / physRatio;
	    frame.origin.y = (frame.size.height - newHeight) / 2;
	    frame.size.height = newHeight;
	}
    }

    [[UIApplication sharedApplication] beginReceivingRemoteControlEvents];

    if (![self becomeFirstResponder]) {
	NSLog(@"!DIDNT BCOME FIRST RESP");
    }

    /* initialize settings first, so they're available to the rest of initialization */
    _topSettings = [[MFANTopSettings alloc] initWithFrame: frame viewController: self];

    /* history needs to exist by time playerview is initialized */
    _topHistory = [[MFANTopHistory alloc] initWithFrame: frame
					  viewController: self];

    _comm = [[MFANComm alloc] init];
    _topLevel = [[MFANTopLevel alloc] initWithFrame: frame viewController: self comm: _comm];
    radioConsole = [_topLevel radioConsole];
    _myPlayerView = [_topLevel playerView];
    for(i=0;i<MFANChannelTypeCount;i++) {
	_topEdit[i] = [[MFANTopEdit alloc] initWithFrame: frame
					   channelType: i
					   level: _topLevel
					   console: radioConsole
					   viewController: self];
    }
    _topHelp = [[MFANTopHelp alloc] initWithFrame: frame viewController: self];

    for(i=0;i<MFANChannelTypeCount;i++) {
	_topList[i] = [[MFANTopList alloc] initWithFrame:frame
					   channelType: i
					   level: _topLevel
					   console: radioConsole
					   viewController: self];
    }
    _topUpnp = [[MFANTopUpnp alloc] initWithFrame: frame viewController: self];
    _topDoc = [[MFANTopDoc alloc] initWithFrame: frame level: _topLevel viewController: self];
    _topMenu = [[MFANTopMenu alloc] initWithFrame: frame viewController: self];

    _level = 1;
    [self makeActive: _topLevel];
}

- (void) makeActive: (UIView<MFANTopApp> *) newView
{
    if (newView != _activeView) {
	[_activeView deactivateTop];
	_activeView = newView;
	[self setView: newView];

	/* doing the activateTop last allows a module's activateTop to run another's instead */
	[newView activateTop];
    }
}

- (void)remoteControlReceivedWithEvent:(UIEvent *)receivedEvent {
    [_myPlayerView remoteControlReceivedWithEvent: receivedEvent];
}

- (void) enterBackground
{
    [_topLevel enterBackground];
}

- (void) leaveBackground
{
    [_topLevel leaveBackground];
}

- (void) swipedRight
{
    MFANTopEdit *app;

    app = _topEdit[_channelType];
    /* move to editing screen */
    [app setSetList: [_topLevel setList]];
    [self makeActive: app];
}

- (void) restorePlayer
{
    [self makeActive: _topLevel];
}

- (void) swipedLeft
{
    [self makeActive: _topLevel];
}

- (id) init
{
    self = [super init];
    /* self may be null */
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    // Do any additional setup after loading the view.
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
    NSLog(@"!Received IOS memory warning!");
}

- (MFANChannelType) channelType
{
    return _channelType;
}

/*
#pragma mark - Navigation

// In a storyboard-based application, you will often want to do a little preparation before navigation
- (void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender
{
    // Get the new view controller using [segue destinationViewController].
    // Pass the selected object to the new view controller.
}
*/

@end
