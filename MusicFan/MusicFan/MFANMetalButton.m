//
//  MFANMetalButton.m
//  MusicFan
//
//  Created by Michael Kazar on 4/23/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANMetalButton.h"
#import "MFANCGUtil.h"

@implementation MFANMetalButton {
    SEL _callback;
    /* pointer up the hierarchy would prevent refcount GC */
    id __weak _context;		/* who to call */
    id __weak _callbackValue;	/* what to pass them */

    int _selected;
    UIColor *_color;
    UIColor *_darkColor;
    BOOL _noMargin;
    UIImage *_silverImage;
    NSString *_title;
}

/* can't call this "selected" since that apparently conflicts with a
 * method in UIView or something.  You'd think we'd get priority, but
 * apparently not.
 */
- (BOOL) isSelected
{
    return _selected;
}

- (void) setSelected: (BOOL) selected
{
    _selected = selected;
    [self setNeedsDisplay];
}

- (void) setNoMargin
{
    _noMargin = YES;
}

- (void) setTitle: (NSString *)title
{
    [self setTitle: title forState: UIControlStateNormal];
    [self setTitle: title forState: UIControlStateSelected];
}

- (id)initWithFrame:(CGRect)frame
	      title: (NSString *) title
	      color:(UIColor *) baseColor
	   fontSize: (CGFloat) fontSize;
{
    CGFloat realFontSize;

    self = [super initWithFrame:frame];
    if (self) {
	CGFloat hue;
	CGFloat sat;
	CGFloat bright;
	CGFloat alpha;

        // Initialization code
	_color = baseColor;
	_title = title;
	[baseColor getHue: &hue saturation: &sat brightness: &bright alpha: &alpha];
	bright = 0.2;
	_darkColor = [UIColor colorWithHue: hue saturation: sat brightness: bright alpha: alpha];
	_selected = NO;
	_noMargin = NO;

	if (fontSize < 0.1) {
	    realFontSize = frame.size.height * 0.5;
	}
	else {
	    realFontSize = fontSize;
	}

	self.titleLabel.textAlignment = NSTextAlignmentCenter;
	// self.titleLabel.font = [UIFont boldSystemFontOfSize: realFontSize];

	_silverImage = resizeImage2( [UIImage imageNamed: @"button.png"],
				     frame.size);
	// [self setImage: _silverImage forState: UIControlStateNormal];

	[self setBackgroundColor: [UIColor clearColor]];
	[self setTitleColor: [UIColor redColor] forState: UIControlStateNormal];
	[self setTitleColor: [UIColor greenColor] forState: UIControlStateSelected];

	if (title != nil) {
	    [self setTitle: title forState: UIControlStateNormal];
	    [self setTitle: title forState: UIControlStateSelected];
	}

	[self addTarget: self
	      action: @selector(buttonPressed:withEvent:)
	      forControlEvents: UIControlEventAllTouchEvents];
    }
    return self;
}

- (void) setFont: (UIFont *) font
{
    self.titleLabel.font = font;
}

- (void) addCallback: (id) context
	  withAction: (SEL) callback
	       value: (id) callbackValue
{
    _callback = callback;
    _callbackValue = callbackValue;
    _context = context;
}

- (void) buttonPressed: (id) buttonp withEvent: (UIEvent *) eventp
{
    NSSet *allTouches;
    NSEnumerator *enm;
    UITouch *touch;
    UITouchPhase phase;
    NSNumber *value;

    if (![eventp respondsToSelector:@selector(allTouches)]) {
	// Bogus event from UIKit!
	return;
    }

    allTouches = [eventp allTouches];
    enm = [allTouches objectEnumerator];
    phase = UITouchPhaseBegan;
    while( touch = [enm nextObject]) {
	phase = [touch phase];
    }

    if (phase == UITouchPhaseBegan) {
	value = [[NSNumber alloc] initWithInt: 0]; /* use this to pass hold vs. touch later on */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
	[_context performSelector: _callback withObject: _callbackValue];
#pragma clang diagnostic pop
    }
}

- (void) drawRect: (CGRect) rect
{
    CGContextRef cx;

    cx = UIGraphicsGetCurrentContext();

    /* finally draw the glossy button */
    [_silverImage drawInRect: rect];

    if (_title == nil) {
	/* draw the circle */
	CGContextSetFillColorWithColor(cx, [UIColor clearColor].CGColor);
	CGContextFillRect(cx, rect);

	/* note must reduce radius if increase line width */
	CGContextAddArc(cx,
			/* center.x */ rect.origin.x+rect.size.width/2,
			/* center.y */ rect.origin.y + rect.size.height/2,
			/* radius */ (rect.size.height/2) * 0.8,
			/* start angle */0,
			/* end angle */ 2 * M_PI,
			/* clockwise */ 1);

	CGContextSetLineWidth(cx, rect.size.height / 12);
	CGContextSetFillColorWithColor(cx, [UIColor clearColor].CGColor);
	if (_selected)
	    CGContextSetStrokeColorWithColor(cx, _color.CGColor);
	else
	    CGContextSetStrokeColorWithColor(cx, _darkColor.CGColor);
	CGContextDrawPath(cx, kCGPathFillStroke);
    }
}

@end
