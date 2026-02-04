#import "TopView.h"
#import "MFANCoreButton.h"
#import "MarqueeLabel.h"

@implementation TopView

- (TopView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *) vc {
    self = [super initWithFrame: frame];
    if (self != nil) {
	CGRect screenFrame = self.frame;

	CGRect signFrame;

	float vertMargin = 25;

	// We reserve vertMargin at the top and bottom of the screen.
	// Then the signFrame gets 90% of the remaining space, the
	// marquee gets the next 5% and the control buttons get the
	// last 10%
	float usableHeight = screenFrame.size.height - 2*vertMargin;
	float usableOriginY = screenFrame.origin.y + vertMargin;

	signFrame = screenFrame;
	signFrame.origin.y = usableOriginY;
	signFrame.size.height = usableHeight * 0.9;
	SignView *signView = [[SignView alloc] initWithFrame: signFrame ViewCont: vc];
	[self addSubview: signView];

	CGRect marqueeFrame = screenFrame;
	marqueeFrame.origin.y = signFrame.origin.y + signFrame.size.height;
	marqueeFrame.size.height = usableHeight * 0.05;
	MarqueeLabel *marquee = [[MarqueeLabel alloc] initWithFrame: marqueeFrame];
	[marquee setTextColor: [UIColor blackColor]];
	[marquee setTextAlignment: NSTextAlignmentCenter];
	[self addSubview: marquee];

	// and put something there.
	[marquee setText: @"[Your ad here]"];
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
	[playButton addCallback: self withAction:@selector(playPressed:withData:)];
	[self addSubview: playButton];
    }

    return self;
}

- (void) playPressed: (id) sender withData: (NSNumber *)movement {
    NSLog(@"in play pressed");
}

@end
