#import "TopView.h"
#import "MFANCoreButton.h"
#import "MFANIconButton.h"
#import "MarqueeLabel.h"
#import "MFANStreamPlayer.h"

@implementation TopView {
    MarqueeLabel *_marquee;
    MFANCoreButton *_playButton;
    MFANIconButton *_stopButton;
    MFANCoreButton *_skipFwdButton;
    MFANCoreButton *_skipBackButton;
    MFANCoreButton *_startButton;
    SignView *_signView;
    RadioHistory *_history;
    ViewController *_vc;
}

- (TopView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *) vc {
    self = [super initWithFrame: frame];
    if (self != nil) {
	CGRect screenFrame = self.frame;

	NSLog(@"FRANE TOPVIEW %fx%f@%f.%f",
	      screenFrame.size.width, screenFrame.size.height,
	      screenFrame.origin.x, screenFrame.origin.y);

	CGRect signFrame;

	float vertMargin = vc.topMargin;

	_vc = vc;

	// We reserve vertMargin at the top and bottom of the screen.
	// Then the signFrame gets 90% of the remaining space, the
	// marquee gets the next 5% and the control buttons get the
	// last 10%
	float usableHeight = screenFrame.size.height - 2*vertMargin;
	float usableOriginY = screenFrame.origin.y + vertMargin;

	signFrame = screenFrame;
	signFrame.origin.y = usableOriginY;
	signFrame.size.height = usableHeight * 0.90;
	SignView *signView = [[SignView alloc] initWithFrame: signFrame ViewCont: vc];
	_signView = signView;
	[self addSubview: signView];

	NSLog(@" FRAME SIGNVIEW %fx%f@%f.%f",
	      signFrame.size.width, signFrame.size.height,
	      signFrame.origin.x, signFrame.origin.y);

	[signView setSongCallback: self sel:@selector(songChanged:)];
	[signView setStateCallback: self sel:@selector(stateChanged:)];

	CGRect startFrame = screenFrame;
	startFrame.origin.y = signFrame.origin.y + signFrame.size.height;
	startFrame.size.height = usableHeight * .05;
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
	[_startButton setClearText: @"Main Menu"];
	[_startButton addCallback: self withAction: @selector(startPressed:)];
	[self addSubview: _startButton];

	CGRect marqueeFrame = screenFrame;
	marqueeFrame.origin.y = startFrame.origin.y + startFrame.size.height;
	marqueeFrame.size.height = usableHeight * 0.05;
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

	CGRect buttonFrame = screenFrame;

	buttonFrame.size.height = usableHeight * 0.05;

	float smallButtonWidth = buttonFrame.size.height;
	float largeButtonWidth = 2*buttonFrame.size.height;

	buttonFrame.size.width = largeButtonWidth;
	buttonFrame.origin.x = screenFrame.size.width/5 - largeButtonWidth/2;
	buttonFrame.origin.y = marqueeFrame.origin.y + marqueeFrame.size.height;

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
    }

    return self;
}

- (void) historyDone: (id) junk {
    [_vc popTopView];
}

- (void) startPressed: (id) junk {
    [_signView displayAppOptions];
}

- (void) skipFwdPressed:(id) junk withData: junk2 {
    NSLog(@"+20");
}

- (void) skipBackPressed:(id) junk withData: junk2 {
    NSLog(@"-20");
}

- (void) stopPressed:(id) junk withData: junk2 {
    [_signView stopRadio];
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

- (void) songChanged: (id) asong {
    NSString *song = (NSString *) asong;
    if (song != nil) {
	[_marquee setText: song];
	[_history addHistoryStation: [_signView getPlayingStationName]
			   withSong: song];
    } else {
	[_marquee setText: @"[Unknown]"];
    }
}

- (void) playPressed: (id) sender withData: (NSNumber *)movement {
    MFANStreamPlayer *player = [_signView getCurrentPlayer];
    if (player == nil) {
	[_signView startCurrentStation];
    } else if ([player isPaused])
	[player resume];
    else
	[player pause];
}

@end
