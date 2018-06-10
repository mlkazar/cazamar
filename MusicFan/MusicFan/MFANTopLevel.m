//
//  MFANTopLevel.m
//  MusicFan
//
//  Created by Michael Kazar on 4/20/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//
#import "MFANTopLevel.h"
#import "MFANIconButton.h"
#import "MFANPlayContext.h"
#import "MFANPlayerView.h"
#import "MFANCoreButton.h"
#import <MediaPlayer/MediaPlayer.h>
#import "MFANSetList.h"
#import "MFANViewController.h"
#import "MFANRadioConsole.h"
#import "MFANCGUtil.h"
#import "MFANTopSettings.h"
#import "MFANPopHelp.h"

#define MFAN_MAXBUTTONS 8

#define SYSTEM_VERSION_GE(v)  ([[[UIDevice currentDevice] systemVersion] compare:v options:NSNumericSearch] != NSOrderedAscending)

@implementation MFANAutoDim {
    float _baseDim;
    NSTimer *_timer;
    long _timerTime;	/* time we last created a timer */
}

- (MFANAutoDim *) init
{
    self = [super init];
    if (self != nil) {
	_baseDim = [[UIScreen mainScreen] brightness];
	_timer = nil;
	_timerTime = osp_get_ms();
    }
    return self;
}

/* make sure timer is active, and restart it; this is called when we know
 * that someone just touched the iPhone screen, and we want to reset
 * to initial brightness.
 */
- (void) enableTimer
{
    long now;

    if (![MFANTopSettings autoDim]) {
	[self disableTimer];
	return;
    }

    now = osp_get_ms();

    /* restore brightness */
    [[UIScreen mainScreen] setBrightness: _baseDim];

    /* make sure timer will go off at the right time */
    if (_timer == nil || now - _timerTime > 2000) {
	if (_timer != nil) {
	    [_timer invalidate];
	    _timer = nil;
	}
	_timer = [NSTimer scheduledTimerWithTimeInterval: 60.0
			  target:self
			  selector:@selector(timerFired:)
			  userInfo:nil
			  repeats: NO];
	return;
    }
}

/* we're switching to a context where the brightness should be restored */
- (void) disableTimer
{
    if (_timer) {
	[_timer invalidate];
	_timer = nil;
    }

    /* restore brightness */
    if ([MFANTopSettings autoDim])
	[[UIScreen mainScreen] setBrightness: _baseDim];
}

- (void) timerFired: (id) junk
{
    _timer = nil;

    if (![MFANTopSettings autoDim])
	return;

    [[UIScreen mainScreen] setBrightness: 0.09];
}

@end

@implementation MFANTopLevel {
    MPMusicPlayerController *_myPlayerp;
    MPMediaQuery *_myQueryp;
    UIImageView *_artView;
    MFANPlayerView *_myPlayerView;
    MFANPopHelp *_popHelp;
    CGRect _appFrame;
    CGFloat _appWidth;
    CGFloat _appVMargin;
    CGFloat _appHMargin;
    CGFloat _appHeight;
    CGFloat _buttonHeight;
    CGRect _playerFrame;
    CGRect _swipeLeftFrame;
    CGRect _listButtonFrame;
    MFANCoreButton *_swipeLeftButton;
    CGRect _swipeRightFrame;
    CGRect _recordButtonFrame;
    CGRect _playNowButtonFrame;
    MFANIconButton *_listButton;
    MFANIconButton *_swipeRightButton;
    MFANIconButton *_playNowButton;
    MFANIconButton *_recordButton;
    MFANPlayContext *_currentContext;
    MFANViewController *_viewCont;
    MFANPlayContext *_playContext[MFAN_MAXBUTTONS];
    MFANPlayContext *_popupContext;
    MFANPlayContext *_savedContext;	/* old context, after popupContext pushed */
    MFANRadioConsole *_radioConsole;
    int _currentRadioIndex;
    MFANAutoDim *_autoDim;
    BOOL _isRecording;
}

int _nbuttons;
int _popupIndex = 100;

- (UIView *) hitTest: (CGPoint) point withEvent: (UIEvent *) event
{
    [_autoDim enableTimer];
    return [super hitTest: point withEvent: event];
}

- (NSArray *) getPlayContexts
{
    NSMutableArray *rar;
    int i;

    rar = [[NSMutableArray alloc] init];
    for(i=0;i<_nbuttons;i++) {
	[rar addObject: _playContext[i]];
    }
    return rar;
}

- (id)initWithFrame:(CGRect)frame
     viewController:(MFANViewController *) viewCont
	       comm:(MFANComm *)comm
{
    int i;
    CGFloat topButtonWidth;
    CGFloat topButtonHeight;
    CGFloat buttonGap;
    CGFloat buttonWidth;
    CGFloat buttonHeight;
    CGFloat verticalGap;
    int bestIx;
    uint64_t bestMTime;
    uint64_t tmtime;
    CGRect consoleFrame;

    self = [super initWithFrame:frame];

    _nbuttons = 4;

    if (self) {
	/* clocks the current brightness, as well */
	_isRecording = NO;
	_autoDim = [[MFANAutoDim alloc] init];

	_artView = nil;
	_appVMargin = 15.0;
	_appHMargin = 2.0;
	verticalGap = 1.0;

	_viewCont = viewCont;

	[self setBackgroundColor: [UIColor whiteColor]];

	/* the part of the screen we use for our application; also
	 * compute appWidth and appHeight for convenience.
	 */
	_appFrame = frame;
	_appFrame.origin.x += _appHMargin;
	_appFrame.origin.y += _appVMargin;
	_appWidth = (_appFrame.size.width -= 2 * _appHMargin);
	_appHeight = (_appFrame.size.height -= _appVMargin);

	/* how to center 2 buttons on screen */
	topButtonWidth = (_appFrame.size.width/4) * 0.9;	/* 90% of available space */
	topButtonHeight = 48;
	float col1of4 = _appFrame.origin.x + _appFrame.size.width/5 - topButtonWidth/2;
	float col2of4 = _appFrame.origin.x + 2*_appFrame.size.width/5 - topButtonWidth/2;
	float col3of4 = _appFrame.origin.x + 3*_appFrame.size.width/5 - topButtonWidth/2;
	float col4of4 = _appFrame.origin.x + 4*_appFrame.size.width/5 - topButtonWidth/2;

	/* the label on the left of the screen */
	_listButtonFrame.origin.x = col1of4;
	_listButtonFrame.origin.y = _appFrame.origin.y;
	_listButtonFrame.size.width = topButtonWidth;
	_listButtonFrame.size.height = topButtonHeight;
	_listButton = [[MFANIconButton alloc] initWithFrame: _listButtonFrame
					      title:@"Channel Details"
					      color: [MFANTopSettings baseColor]
					      file: @"icon-edit.png"];
	// could pass setTextAlignment: NSTextAlignmentRight through, but don't need to
	[self addSubview: _listButton];
	[_listButton addCallback: self withAction: @selector(detailsPressed:)];

	_playNowButtonFrame.origin.x = col2of4;
	_playNowButtonFrame.origin.y = _appFrame.origin.y;
	_playNowButtonFrame.size.width = topButtonWidth;
	_playNowButtonFrame.size.height = topButtonHeight;
	_playNowButton = [[MFANIconButton alloc] initWithFrame: _playNowButtonFrame
						title:@"PlayNow"
						color: [MFANTopSettings baseColor]
						file: @"icon-playnow.png"];
	[_playNowButton setSelectedInfo: 1.0 color: [UIColor greenColor]];
	[self addSubview: _playNowButton];
	[_playNowButton addCallback: self withAction: @selector(playNowPressed:)];

	_recordButtonFrame.origin.x = col3of4;
	_recordButtonFrame.origin.y = _appFrame.origin.y;
	_recordButtonFrame.size.width = topButtonWidth;
	_recordButtonFrame.size.height = topButtonHeight;
	_recordButton = [[MFANIconButton alloc] initWithFrame: _recordButtonFrame
						title:@"Record"
						color: [MFANTopSettings baseColor]
						file: @"icon-record.png"];
	[_recordButton setSelectedInfo: 0.5 color: [UIColor orangeColor]];
	[_recordButton setSelectedTitle: @"Recording"];
	[self addSubview: _recordButton];
	[_recordButton addCallback: self withAction: @selector(recordPressed:)];

	/* the label on the right of the screen */
	_swipeRightFrame.origin.x = col4of4;
	_swipeRightFrame.origin.y = _appFrame.origin.y;
	_swipeRightFrame.size.width = topButtonWidth;
	_swipeRightFrame.size.height = topButtonHeight;
	_swipeRightButton = [[MFANIconButton alloc] initWithFrame: _swipeRightFrame
						    title:@"Menu"
						    color: [MFANTopSettings baseColor]
						    file: @"icon-more.png"];
	// could pass setTextAlignment: NSTextAlignmentRight through, but don't need to
	[self addSubview: _swipeRightButton];
	[_swipeRightButton addCallback: self withAction: @selector(menuPressed:)];

	/* next figure out how much space we reserve for the radio buttons at the
	 * bottom of the app.
	 */
	buttonGap = _appWidth / 8 / _nbuttons;
	buttonWidth = (_appWidth - (_nbuttons-1) * buttonGap) /_nbuttons;
	buttonHeight = 40.0;

	/* the player frame goes from the top to the radio buttons,
	 * and also includes a vertical gap between each view.  Note
	 * that it has a clear background and only put text near the
	 * top in the center, so the swipe labels can overlap it.
	 *
	 * The topMargin tells the playerView how much space to leave
	 * for the buttons at the top.
	 */
	_playerFrame.origin.x = _appFrame.origin.x;
	_playerFrame.origin.y = _appFrame.origin.y;
	_playerFrame.size.width = _appFrame.size.width;
	_playerFrame.size.height = _appHeight - 2*verticalGap - buttonHeight;

	_myPlayerp = [MPMusicPlayerController systemMusicPlayer];
	_myPlayerView = [[MFANPlayerView alloc] initWithParent: self
						topMargin: topButtonHeight+4
						comm: comm
						viewCon: _viewCont
						andFrame: _playerFrame];
	[_myPlayerView setHistory: (MFANTopHistory *) [_viewCont getTopAppByName: @"history"]];

	/* next layout the radio buttons */
	bestMTime = 0;
	bestIx = 0;
	for(i=0;i<_nbuttons;i++) {
	    /* set backpointer */
	    _playContext[i] = [[MFANPlayContext alloc] initWithButton: nil
						       withPlayerView: _myPlayerView
						       withView: self
						       withFile: [self fileNameForIndex: i]];
	    tmtime = [_playContext[i] mtime];
	    if (tmtime > bestMTime) {
		bestIx = i;
		bestMTime = tmtime;
	    }
	} /* loop over all buttons */
	_popupContext = [[MFANPlayContext alloc] initWithButton: nil
						 withPlayerView: _myPlayerView
						 withView: self
						 withFile: [self fileNameForIndex: _popupIndex]];

	/* if we're starting up, use the first channel to avoid confusion */
	if (![self hasAnyItems]) {
	    bestIx = 0;
	}

	_popHelp = [[MFANPopHelp alloc] initWithFrame: _appFrame
					helpFile: @"help-top"
					parentView: self
					warningFlag: MFANTopSettings_warnedTop];

	consoleFrame.size.width = _appFrame.size.width;
	consoleFrame.size.height = buttonHeight;
	consoleFrame.origin.x = _appFrame.origin.x;
	consoleFrame.origin.y = ( _appFrame.origin.y +
				  _appFrame.size.height -
				  consoleFrame.size.height);
	_radioConsole = [[MFANRadioConsole alloc]
			    initWithFrame: consoleFrame
			    buttonCount: _nbuttons];
	/* we'll be passed the button's context as well */
	[_radioConsole addCallback: self action: @selector(buttonPressed:)];
	[self addSubview: _radioConsole];

	_currentContext = _playContext[bestIx];
	[_currentContext setSelected: YES];
	[_radioConsole setSelected: bestIx];
	_currentRadioIndex = bestIx;
	[_currentContext play];
    }
    return self;
}

- (MFANPlayerView *) playerView
{
    return _myPlayerView;
}

- (MFANPlayContext *) getContextAtIndex: (int) ix
{
    if (ix < _nbuttons)
	return _playContext[ix];
    else
	return nil;
}

- (BOOL) hasAnyItems
{
    int i;
    for(i=0;i<_nbuttons;i++)
	if ([_playContext[i] hasAnyItems])
	    return YES;
    return NO;
}

- (MFANRadioConsole *) radioConsole
{
    return _radioConsole;
}

- (MFANSetList *) setList
{
    return [_currentContext setList];
}

- (void) activateTop {
    /* popup per-screen help */
    [_popHelp checkShow];

    [_radioConsole removeFromSuperview];
    [self addSubview: _radioConsole];
    [_radioConsole addCallback: self action: @selector(buttonPressed:)];
    [_radioConsole setSelected: _currentRadioIndex];
    [_autoDim enableTimer];
}

- (void) deactivateTop {
    _currentRadioIndex = [_radioConsole selected];
}

- (MFANPlayContext *) currentContext
{
    [_autoDim disableTimer];
    return _currentContext;
}

- (void) setCurrentContext: (MFANPlayContext *) newContext
{
    _currentContext = newContext;
}

- (MFANPlayContext *) popupContext
{
    return _popupContext;
}

- (void) playCurrent
{
    if (_currentContext != nil) {
	[_currentContext play];
    }
}

- (void) recordPressed: (id) arg
{
    int code;

    if (_isRecording) {
	_isRecording = NO;
	[_myPlayerView stopRecording];
	[_recordButton setSelected: NO];
    }
    else {
	code = [_myPlayerView startRecordingFor: self sel:@selector(recordingStopped)];
	if (code == 0) {
	    _isRecording = YES;
	    [_recordButton setSelected: YES];
	}
    }
    NSLog(@"isrecording is now %d", _isRecording);
}

/* called back from recording if something makes the recording stop */
- (void) recordingStopped
{
    [_recordButton setSelected: NO];
    _isRecording = NO;
}

- (void) playNowPressed: (id) arg
{
    MFANTopEdit *edit;
    NSMutableArray *ar = [[NSMutableArray alloc] init];

    if (_currentContext != _popupContext) {
	_savedContext = _currentContext;
	[_currentContext pause];
	[_playNowButton setSelected: YES];
	[_currentContext setSelected: NO];

	_currentContext = _popupContext;
	[_currentContext setSelected: YES];

	/* clear current contents */
	[_popupContext setQueryInfo: ar];	/* changes setList's itemArray */

	/* edit updates the TopLevel's current context, set above */
	edit = (MFANTopEdit *) [_viewCont getTopAppByName: @"edit"];
	[edit setBridgeCallback: self withAction: @selector(playNowAfterEdit:)];

	/* and make edit screen top level */
	[_viewCont switchToAppByName: @"edit"];

	[edit bridgeAddPressed];
    }
    else {
	[_playNowButton setSelected: NO];

	/* stop this one */
	[_currentContext pause];
	[_currentContext setSelected: NO];

	/* and start this one */
	_currentContext = _savedContext;
	[_currentContext setSelected: YES];
	[_currentContext play];
    }
}    

- (void) playNowAfterEdit: (id) junk
{
    [_popupContext play];
}

- (void) detailsPressed: (id) arg
{
    [_viewCont switchToAppByName: @"list"];
}

- (void) helpPressed: (id) arg
{
    [_viewCont switchToAppByName: @"help"];
}

- (void) listPressed: (id) arg
{
    if ([_currentContext hasAnyItems])
	[_viewCont switchToAppByName: @"list"];
    else
	[_viewCont switchToAppByName: @"edit"];
}

- (void) socialPressed: (id) arg
{
    [_viewCont switchToAppByName: @"social"];
}

- (void) menuPressed: (id) arg
{
    [_viewCont switchToAppByName: @"menu"];
}

- (void) buttonPressed: (id) acontext
{
    NSNumber *contextInteger = acontext;
    long button;
    MFANPlayContext *newContext;

    button = [contextInteger integerValue];
    if (button >= _nbuttons) {
	NSLog(@"!MFANTopLevel buttonPressed -- bad button context %ld", button);
	return;
    }
    newContext = _playContext[button];

    if (newContext != _currentContext) {
	[_currentContext pause];
	[_currentContext setSelected: NO];
    }

    _currentContext = newContext;
    _currentRadioIndex = (int) button;

    [newContext setSelected: YES];

    /* turn off indicator that we're using the popup context */
    [_playNowButton setSelected: NO];

    [newContext play];
}

- (void) enterBackground
{
    NSLog(@"- Entering background");
    if (_currentContext != nil) {
	/* this queues an asynchronous save; AppDelegate will wait 15
	 * seconds for the save to happen before telling iOS that
	 * we're done.
	 */
	[_currentContext saveListToFile];
    }

    [_autoDim disableTimer];
}

/* enterForeground */
- (void) leaveBackground
{
    [_autoDim enableTimer];
}

/* We're also a text view delegate */
- (BOOL)textFieldShouldReturn:(UITextField *)textField
{
    // [self becomeFirstResponder];    
    return YES;
}

- (NSString *) fileNameForIndex: (int) ix
{
    NSArray *paths;
    NSString *libdir;
    NSString *filePath;

    paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    filePath = [NSString stringWithFormat: @"%@/button%03d.state", libdir, ix];

    return filePath;
}

// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
- (void)drawRect:(CGRect)rect
{
    // drawBackground(rect);
}


@end
