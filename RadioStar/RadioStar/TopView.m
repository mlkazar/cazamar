#import "TopView.h"
#import "MFANCoreButton.h"
#import "MarqueeLabel.h"
#import "MFANStreamPlayer.h"

@implementation TopView {
    MarqueeLabel *_marquee;
    MFANCoreButton *_playButton;
    SignView *_signView;
}

- (TopView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *) vc {
    self = [super initWithFrame: frame];
    if (self != nil) {
	CGRect screenFrame = self.frame;

	CGRect signFrame;

	float vertMargin = vc.topMargin;

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

	[signView setSongCallback: self sel:@selector(songChanged:)];
	[signView setStateCallback: self sel:@selector(stateChanged:)];

	CGRect marqueeFrame = screenFrame;
	marqueeFrame.origin.y = signFrame.origin.y + signFrame.size.height;
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

	CGRect playButtonFrame = screenFrame;
	playButtonFrame.size.height = usableHeight * 0.05;
	playButtonFrame.size.width = playButtonFrame.size.height; // use square frame
	playButtonFrame.origin.x = (screenFrame.size.width - playButtonFrame.size.width) / 2;
	playButtonFrame.origin.y = marqueeFrame.origin.y + marqueeFrame.size.height;
	MFANCoreButton *playButton = [[MFANCoreButton alloc]
					      initWithFrame: playButtonFrame
						      title:@"Play"
						      color: [UIColor blackColor]];
	_playButton = playButton;
	[playButton addCallback: self withAction:@selector(playPressed:withData:)];
	[self addSubview: playButton];
    }

    return self;
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
    } else {
	[_marquee setText: @"[None]"];
    }
}

- (void) playPressed: (id) sender withData: (NSNumber *)movement {
    MFANStreamPlayer *player = [_signView getCurrentPlayer];
    if (player == nil)
	return;
    if ([player isPaused])
	[player resume];
    else
	[player pause];
}

@end
