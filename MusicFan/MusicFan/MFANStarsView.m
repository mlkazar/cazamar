//
//  MFANStarsView.m
//  MusicFan
//
//  Created by Michael Kazar on 5/29/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANStarsView.h"
#import "MFANTopSettings.h"

@implementation MFANStarsView {
    /* _enabledCount is property */
    UIColor *_activeColor;
    UIColor *_inactiveColor;
    CGFloat _width;
    NSTimer *_timer;
    
    id _callbackObj;
    SEL _callbackSel;
}

- (id)initWithFrame:(CGRect)frame background: (BOOL) initBackground
{
    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	_enabledCount = 0;
	_timer = nil;
	_callbackObj = nil;
	_callbackSel = nil;
	_activeColor = [MFANTopSettings lightBaseColor];
	_inactiveColor = [UIColor grayColor];
	if (initBackground)
	    self.backgroundColor = [UIColor whiteColor];
	else
	    self.backgroundColor = [UIColor clearColor];
	_width = frame.size.width;

	_timer = [NSTimer scheduledTimerWithTimeInterval: 5.0
			  target: self
			  selector: @selector(passHit:)
			  userInfo: nil
			  repeats: NO];

    }
    return self;
}

- (void) addCallback: (id) callbackObj
	  withAction: (SEL) callbackSel
{
    _callbackObj = callbackObj;
    _callbackSel = callbackSel;
}

- (void) touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event
{
    UITouch *aTouch = [touches anyObject];
    float segmentIx;

    CGPoint point = [aTouch locationInView:self];
    /* point.x and point.y have the coordinates of the touch.  The
     * width is divided into 15 segments.  Each star is 2 segments (s)
     * wide, with a segment of space to the left.  If you press with x
     * < s, we want to send 0 (stars); s < x < 3.5s send 1; 3.5s < x <
     * 6.5s send 2; 6.5s < x < 9.5s send 3; 9.5s < x < 12.5s send 4;
     * 12.5s < x < 15.5s sends 5 (though x < 15s).
     */
    segmentIx = (15 * point.x) / _width;
    if (segmentIx < 1.0)
	_enabledCount = 0;
    else if (segmentIx < 3.5)
	_enabledCount = 1;
    else {
	_enabledCount = ((int) (segmentIx - 3.5) / 3.0) + 2;
    }
    if (_enabledCount > 5)	/* just in case floating point edge case */
	_enabledCount = 5;

    [self setNeedsDisplay];

    if (_callbackObj != nil) {
	/* get rid of long timer */
	if (_timer) {
	    [_timer invalidate];
	    _timer = nil;
	}

	_timer = [NSTimer scheduledTimerWithTimeInterval: 1.5
			  target: self
			  selector: @selector(passHit:)
			  userInfo: nil
			  repeats: NO];
	
    }
}

- (void) clearCallback
{
    _callbackObj = nil;
    _callbackSel= nil;
    if (_timer == nil) {
	[_timer invalidate];
	_timer = nil;
    }
}

- (void) passHit: (id) context
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    if (_callbackObj != nil)
	[_callbackObj performSelector: _callbackSel withObject: nil];
#pragma clang diagnostic pop
}

-(void)drawRect:(CGRect)rect
{
    CGContextRef cxp = UIGraphicsGetCurrentContext();

    // constants
    const float w = rect.size.width;
    const float r = w/15;
    const double theta = 2 * M_PI * (2.0 / 5.0);
    const float flip = -1.0f; // flip vertically (default star representation)
    long count = 5;
    UIColor *color;

    // drawing center for the star
    float xCenter = 2*r;

    /* star's Y height is 2*r, and total Y height is rect.size.height,
     * so we want to add an extra rect.size.height/2 - r to the Y
     * position.
     */

    CGContextSetFillColorWithColor(cxp, [UIColor clearColor].CGColor);
    CGContextFillRect(cxp, rect);

    for (NSUInteger i=0; i<count; i++)  {
	color = ((i>=_enabledCount)? _inactiveColor : _activeColor);

	// get star style based on the index
	CGContextSetFillColorWithColor(cxp, color.CGColor);
	CGContextSetStrokeColorWithColor(cxp, color.CGColor);

	// update position
	CGContextMoveToPoint(cxp, xCenter, r * flip + rect.size.height/2);

	// draw the necessary star lines
	for (NSUInteger k=1; k<5; k++) {
	    float x = r * sin(k * theta);
	    float y = r * cos(k * theta);
	    CGContextAddLineToPoint(cxp, x + xCenter, y * flip + rect.size.height/2 /* was r */);
	}

	// update horizontal center for the next star
	xCenter += 3*r;

	// draw current star
	CGContextClosePath(cxp);
	CGContextFillPath(cxp);
	CGContextStrokePath(cxp);
    }
}
/*
// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
- (void)drawRect:(CGRect)rect
{
    // Drawing code
}
*/

@end
