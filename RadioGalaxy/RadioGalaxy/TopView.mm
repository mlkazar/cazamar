#import "BufferSlider.h"
#import "TopView.h"
#import "MFANCoreButton.h"
#import "MFANIconButton.h"
#import "MarqueeLabel.h"
#import "MFANStreamPlayer.h"
#import "Settings.h"

@implementation TopAlert {
    NSTimer *_timer;
    UIAlertController *_alert;
}

- (TopAlert *) initWithMessage: (NSString *) message
		      duration: (float) duration
		      viewCont: (ViewController *) vc {
    self = [super init];
    if (self != nil) {
	_alert = [UIAlertController alertControllerWithTitle: @"RadioGalaxy"
						     message: message
					      preferredStyle: UIAlertControllerStyleAlert];

	UIAlertAction *action = [UIAlertAction actionWithTitle:@"OK"
							 style: UIAlertActionStyleDefault
						       handler:^(UIAlertAction *act) {
		[self->_alert dismissViewControllerAnimated: YES completion: nil];
	    }];

	[_alert addAction: action];

	[NSTimer scheduledTimerWithTimeInterval: duration
					 target: self
				       selector: @selector(dismissAlert:)
				       userInfo: nil
					repeats: NO];

	[vc presentViewController: _alert animated:YES completion: nil];
    }

    return self;
}

- (void) dismissAlert: (id) junk {
    [_alert dismissViewControllerAnimated: YES completion: nil];
}

@end

@implementation TopView {
    MarqueeLabel *_marquee;
    MFANCoreButton *_playButton;
    MFANIconButton *_stopButton;
    MFANCoreButton *_skipFwdButton;
    MFANCoreButton *_skipBackButton;
    MFANCoreButton *_startButton;
    MFANCoreButton *_addButton;
    MFANCoreButton *_highlightButton;
    SignView *_signView;
    RadioHistory *_history;
    ViewController *_vc;
    NSMutableDictionary *_nowPlayingInfo;
    BufferSlider *_sliderView;
    Settings *_settings;
}

- (TopView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *) vc {
    self = [super initWithFrame: frame];
    if (self != nil) {
	CGRect screenFrame = self.frame;
	screenFrame.origin.y = 0;	// because these are relative to parent view

	NSLog(@"FRANE TOPVIEW %fx%f@%f.%f",
	      screenFrame.size.width, screenFrame.size.height,
	      screenFrame.origin.x, screenFrame.origin.y);

	_vc = vc;

	// load this early so other init functions can make use of it.
	_settings = [[Settings alloc] initWithViewController: _vc];
	_vc.settings = _settings;

	CGRect signFrame;

	// We reserve vertMargin at the top and bottom of the screen.
	// Then the signFrame gets 90% of the remaining space, the
	// marquee gets the next 5% and the control buttons get the
	// last 10%
	float usableHeight = screenFrame.size.height;

	signFrame = screenFrame;
	signFrame.size.height = usableHeight * 0.80;

	// remainder from signFrame height, divided by # of bars
	float barHeight = (1.0-0.80) / 4;

	SignView *signView = [[SignView alloc] initWithFrame: signFrame ViewCont: vc];
	_signView = signView;
	[self addSubview: signView];

	NSLog(@" FRAME SIGNVIEW %fx%f@%f.%f",
	      signFrame.size.width, signFrame.size.height,
	      signFrame.origin.x, signFrame.origin.y);

	[signView setSongCallback: self sel:@selector(songChanged:)];
	[signView setStateCallback: self sel:@selector(stateChanged:)];

	UIColor *borderColor = [UIColor colorWithRed: 0.0
					       green: 0.5
						blue: 0.0
					       alpha: 1.0];

	CGRect startFrame = screenFrame;
	startFrame.origin.y = signFrame.size.height;
	startFrame.size.height = usableHeight * barHeight;
	startFrame.size.width = screenFrame.size.width / 3;
	MFANCoreButton *_addButton= [[MFANCoreButton alloc]
					  initWithFrame: startFrame
						  title: @"None"
						  color: [UIColor blackColor]
					backgroundColor: [UIColor greenColor]];
	[_addButton setBackgroundColor:
		 [UIColor colorWithRed: 0.0
				 green: 0.75
				  blue:0.0
				 alpha: 1.0]];
	_addButton.layer.borderWidth = 2.0;
	_addButton.layer.borderColor = borderColor.CGColor;
	[_addButton setClearText: @"+ Station"];
	[_addButton addCallback: self withAction: @selector(addPressed:)];
	[self addSubview: _addButton];

	startFrame.origin.x += screenFrame.size.width/3;
	MFANCoreButton *_highlightButton= [[MFANCoreButton alloc]
					     initWithFrame: startFrame
						     title: @"None"
						     color: [UIColor blackColor]
					   backgroundColor: [UIColor greenColor]];
	[_highlightButton setBackgroundColor:
		      [UIColor colorWithRed: 0.0
				      green: 0.75
				       blue:0.0
				      alpha: 1.0]];
	_highlightButton.layer.borderWidth = 2.0;
	_highlightButton.layer.borderColor = borderColor.CGColor;
	[_highlightButton setClearText: @"Highlight"];
	[_highlightButton addCallback: self withAction: @selector(highlightPressed:)];
	[self addSubview: _highlightButton];

	startFrame.origin.x += screenFrame.size.width/3;

	MFANCoreButton *_startButton= [[MFANCoreButton alloc]
					     initWithFrame: startFrame
						     title: @"None"
						     color: [UIColor blackColor]
					   backgroundColor: [UIColor greenColor]];
	[_startButton setBackgroundColor:
		   [UIColor colorWithRed: 0.0
				   green: 0.75
				    blue:0.0
				   alpha: 1.0]];
	_startButton.layer.borderWidth = 2.0;
	_startButton.layer.borderColor = borderColor.CGColor;
	[_startButton setClearText: @"More..."];
	[_startButton addCallback: self withAction: @selector(startPressed:)];
	[self addSubview: _startButton];

	CGRect marqueeFrame = screenFrame;
	marqueeFrame.origin.y = startFrame.origin.y + startFrame.size.height;
	marqueeFrame.size.height = usableHeight * barHeight;
	MarqueeLabel *marquee = [[MarqueeLabel alloc] initWithFrame: marqueeFrame];
	_marquee = marquee;
	[marquee setTextColor: [UIColor blackColor]];
	[marquee setTextAlignment: NSTextAlignmentCenter];
	[marquee setFont: [UIFont fontWithName: @"Arial-BoldMT" size: 30]];
	[self addSubview: marquee];
	NSLog(@"setting marquee frame to y=%f height=%f",
	      marqueeFrame.origin.y, marqueeFrame.size.height);

	// and put something there.
	[marquee setNeedsDisplay];

	CGRect sliderFrame;
	sliderFrame = marqueeFrame;
	sliderFrame.origin.y = marqueeFrame.origin.y + marqueeFrame.size.height;
	sliderFrame.size.height = usableHeight * barHeight;

	_sliderView = [[BufferSlider alloc] initWithFrame: (CGRect) sliderFrame
						 viewCont: (ViewController *) vc
						 signView: (SignView *) signView];
	[self addSubview: _sliderView];

	CGRect buttonFrame = sliderFrame;
	buttonFrame.origin.y += usableHeight * barHeight;

	float smallButtonWidth = buttonFrame.size.height;
	float largeButtonWidth = 2*buttonFrame.size.height;

	buttonFrame.size.width = largeButtonWidth;
	buttonFrame.origin.x = screenFrame.size.width/5 - largeButtonWidth/2;

	_skipBackButton = [[MFANCoreButton alloc]
			      initWithFrame: buttonFrame
				      title: @"Blank"
				      color: [UIColor blackColor]];
	[_skipBackButton addCallback: self withAction:@selector(skipBackPressed:withData:)];
	[_skipBackButton setClearText: @"-20"];
	[self addSubview: _skipBackButton];

	buttonFrame.size.width = smallButtonWidth;
	buttonFrame.origin.x = 2*screenFrame.size.width/5 - smallButtonWidth/2;
	MFANCoreButton *playButton = [[MFANCoreButton alloc]
					      initWithFrame: buttonFrame
						      title:@"Play"
						      color: [UIColor blackColor]];
	_playButton = playButton;
	[playButton addCallback: self withAction:@selector(playPressed:withData:)];
	[self addSubview: playButton];

	buttonFrame.size.width = smallButtonWidth;
	buttonFrame.origin.x = 3*screenFrame.size.width/5 - smallButtonWidth/2;
	_stopButton = [[MFANIconButton alloc]
			  initWithFrame: buttonFrame
				  title: @""
				  color: [UIColor blackColor]
				   file:@"icon-stop.png"];
	[_stopButton addCallback: self withAction: @selector(stopPressed:withData:)];
	[self addSubview: _stopButton];

	buttonFrame.size.width = largeButtonWidth;
	buttonFrame.origin.x = 4*screenFrame.size.width/5 - largeButtonWidth/2;
	_skipFwdButton = [[MFANCoreButton alloc]
			      initWithFrame: buttonFrame
				      title: @"Blank"
				      color: [UIColor blackColor]];
	[_skipFwdButton addCallback: self withAction:@selector(skipFwdPressed:withData:)];
	[_skipFwdButton setClearText: @"+20"];
	[self addSubview: _skipFwdButton];

	_history = [[RadioHistory alloc] initWithViewController:vc];
	[_history setCallback: self WithSel: @selector(historyDone:)];

	[signView setRadioHistory: _history];

	[self setBackgroundColor: [UIColor whiteColor]];

	[[UIApplication sharedApplication] beginReceivingRemoteControlEvents];
    }

    return self;
}

- (void) historyDone: (id) junk {
    [_vc popTopView];
}

- (void) startPressed: (id) junk {
    [_signView displayAppOptions];
}

- (void) addPressed: (id) junk {
    [_signView performAddOperation];
}

- (void) highlightPressed: (id) junk {
    SignStation *playingStation = _signView.playingStation;
    if (playingStation == nil) {
	[_signView.history toggleHighlightInStation:playingStation.stationName];
	(void) [[TopAlert alloc] initWithMessage: @"No song playing"
					duration: 2.0
					viewCont: _vc];
    }

    if (![_signView.history isHighlightedInStation:playingStation.stationName]) {
	[_signView.history toggleHighlightInStation:playingStation.stationName];
	(void) [[TopAlert alloc] initWithMessage: @"Highlighted song in history"
					duration: 2.0
					viewCont: _vc];
    } else {
	(void) [[TopAlert alloc] initWithMessage: @"Song already highlighted in history. "
				 @"Unhighlight via history menu item"
					duration: 2.0
					viewCont: _vc];
    }
}

- (void) skipFwdPressed:(id) junk withData: junk2 {
    NSLog(@"+20");
    [_signView seek: +20.0 relative: true];
}

- (void) skipBackPressed:(id) junk withData: junk2 {
    NSLog(@"-20");
    [_signView seek: -20.0 relative: true];
}

- (void) stopPressed:(id) junk withData: junk2 {
    [_signView stopRadioResumeAtEnd];
    NSLog(@"STOP");
}

- (void) stateChanged: (id) aplayer {
    MFANStreamPlayer *player = (MFANStreamPlayer *) aplayer;
    NSLog(@"in state changed player=%p isPlaying=%d", player, [player isPlaying]);
    if ([player isPlaying])
	[_playButton setTitle:@"Pause"];
    else
	[_playButton setTitle:@"Play"];
}

- (void) updateIOSCenter: (NSString *) song {
    _nowPlayingInfo = [[NSMutableDictionary alloc] init];
    [_nowPlayingInfo setObject: [NSNumber numberWithDouble: 1.0]
			forKey: MPNowPlayingInfoPropertyPlaybackRate];

    [_nowPlayingInfo setObject: [NSNumber numberWithFloat: 0.0]
			forKey: MPNowPlayingInfoPropertyElapsedPlaybackTime];
    [_nowPlayingInfo setObject: [NSNumber numberWithFloat: 300.0]
			forKey: MPMediaItemPropertyPlaybackDuration];

    [_nowPlayingInfo setObject: [NSNumber numberWithUnsignedInt: 1]
			forKey: MPNowPlayingInfoPropertyPlaybackQueueIndex];
    /* internet radio, queue should be really large, so that
     * we never have queueIndex bigger than queueCount.
     */
    [_nowPlayingInfo setObject: [NSNumber numberWithUnsignedInt:
					      (int) 2000]
			forKey: MPNowPlayingInfoPropertyPlaybackQueueCount];
    if (song != nil)
	[_nowPlayingInfo setObject: song forKey: MPMediaItemPropertyTitle];

    [[MPNowPlayingInfoCenter defaultCenter] setNowPlayingInfo: _nowPlayingInfo];
}

- (void) songChanged: (id) asong {
    NSString *song = (NSString *) asong;
    NSString *stationName = [_signView getPlayingStationName];
    NSString *displayName;

    if (song == nil)
	song = @"[Unknown]";

    if ([stationName length] > 18) {
	displayName = [NSString stringWithFormat: @"%@ - %@",
				[stationName substringToIndex: 18],
				song];
    } else {
	displayName = [NSString stringWithFormat: @"%@ - %@", stationName, song];
    }

    [self updateIOSCenter: displayName];
    [_history addHistoryStation: stationName
		       withSong: song];

    [_marquee setText: song];
}

- (void) playInternal {
    MFANStreamPlayer *player = [_signView getCurrentPlayer];
    if (player == nil) {
	[_signView startCurrentStation];
    } else if ([player isPaused])
	[player resume];
    else
	[player pause];
}

- (void) playPressed: (id) sender withData: (NSNumber *)movement {
    [self playInternal];
}

- (void) activateTopView {
    [_marquee restartLabel];

    // let signview know it is active again
    [_signView activateTopView];
}

- (void) deactivateTopView {
    return;
}

- (void)remoteControlReceivedWithEvent:(UIEvent *)receivedEvent {
    NSLog(@"- remotecontrolev = %d", (int) receivedEvent.type);
    if (receivedEvent.type == UIEventTypeRemoteControl) {
        switch (receivedEvent.subtype) {
            case UIEventSubtypeRemoteControlPlay:
            case UIEventSubtypeRemoteControlPause:
            case UIEventSubtypeRemoteControlTogglePlayPause:
		[self playInternal];
                break;

            case UIEventSubtypeRemoteControlPreviousTrack:
		[_signView changeStationBy: -1];
                break;

            case UIEventSubtypeRemoteControlNextTrack:
		[_signView changeStationBy: 1];
                break;

            default:
		NSLog(@"!RMT mystery pressed %d", (int) receivedEvent.subtype);
                break;
        }

	[[UIApplication sharedApplication] beginReceivingRemoteControlEvents];
    }
}

@end
