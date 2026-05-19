#import "HelpLabel.h"

/* HelpLabels are actually UIButtons, so you can use them as such for
 * adjust appearances.
 */
@implementation HelpLabel {
    id _target;
    SEL _sel;
}

- (void) doSetup {
    self.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
    self.contentVerticalAlignment = UIControlContentVerticalAlignmentTop;
    [self setTitleColor: [UIColor blackColor]
	       forState: UIControlStateNormal];
    [self setSelected: YES];
}

- (HelpLabel *) initWithFrame: (CGRect) frame target: (id) target selector:(SEL) selector
{
    self = [super initWithFrame: frame];
    if (self) {
	_target = target;
	_sel = selector;

	[self addTarget: self
	      action: @selector(buttonPressed:withEvent:)
	      forControlEvents: UIControlEventAllTouchEvents];

	[self doSetup];
    }

    return self;
}

- (HelpLabel *) initWithTarget: (id) target selector:(SEL) selector
{
    self = [super init];
    if (self) {
	_target = target;
	_sel = selector;

	[self addTarget: self
	      action: @selector(buttonPressed:withEvent:)
	      forControlEvents: UIControlEventAllTouchEvents];

	[self doSetup];
    }

    return self;
}

- (void) buttonPressed: (id) button withEvent: (UIEvent *) event
{
    NSSet *allTouches;
    NSEnumerator *en;
    UITouch *touch;
    UITouchPhase phase;

    if (![event respondsToSelector:@selector(allTouches)]) {
	/* BOGUS event */
	return;
    }

    allTouches = [event allTouches];
    en = [allTouches objectEnumerator];
    phase = UITouchPhaseBegan;
    while( touch = [en nextObject]) {
	phase = [touch phase];
    }

    if (phase == UITouchPhaseBegan) {
	[_target performSelectorOnMainThread: _sel
				  withObject: nil
			       waitUntilDone: true];
    }
}
@end
