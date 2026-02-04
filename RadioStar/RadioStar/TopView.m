#import "TopView.h"

@implementation TopView

- (TopView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *) vc {
    self = [super initWithFrame: frame];
    if (self != nil) {
	CGRect screenFrame = self.frame;

	CGRect signFrame;

	float vertMargin = 60;

	signFrame = screenFrame;
	signFrame.origin.y += vertMargin;
	signFrame.size.height = signFrame.size.height * .8 - 2*vertMargin;
	SignView *signView = [[SignView alloc] initWithFrame: signFrame ViewCont: vc];
	[self addSubview: signView];
    }

    return self;
}

@end
