//
//  MFANRButton.m
//  MusicFan
//
//  Created by Michael Kazar on 4/23/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANRButton.h"
#import "MFANCGUtil.h"

@implementation MFANRButton {
    SEL _callback;
    /* pointer up the hierarchy would prevent refcount GC */
    id __weak _context;		/* who to call */
    id __weak _callbackValue;	/* what to pass them */

    int _selected;
    UIColor *_colorp;
    float _hueOffset;
    BOOL _noMargin;
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
    UILabel *label;

    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	_colorp = baseColor;
	_selected = NO;
	_noMargin = NO;
	_hueOffset = 0.5;

	if (fontSize < 0.1) {
	    realFontSize = frame.size.height * 0.5;
	}
	else {
	    realFontSize = fontSize;
	}

	// self.titleLabel.textAlignment = NSTextAlignmentCenter;
	self.titleLabel.font = [UIFont boldSystemFontOfSize: realFontSize];

	[self setBackgroundColor: [UIColor clearColor]];
	[self setTitleColor: [UIColor blackColor] forState: UIControlStateNormal];
	[self setTitleColor: [UIColor blackColor] forState: UIControlStateSelected];

	[self setTitle: title forState: UIControlStateNormal];
	[self setTitle: title forState: UIControlStateSelected];

	label = [self titleLabel];
	label.lineBreakMode = NSLineBreakByWordWrapping;
	label.textAlignment = NSTextAlignmentCenter;

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
    CGFloat hue;
    CGFloat sat;
    CGFloat bright;
    CGFloat alpha;
    CGFloat junk;
    CGFloat rgbValues[8];
    UIColor *darkColor;
    CGRect buttonFrame;
    CGRect barFrame;
    CGContextRef cx;
    CGFloat margin;
    CGFloat borderHue;
    UIColor *shadowColor;

    /* _colorp has the bright version of the color, and we create a dimmer one by
     * cutting brightness in half.
     */
    [_colorp getRed: &rgbValues[0] green:&rgbValues[1] blue: &rgbValues[2] alpha: &junk];
    rgbValues[3] = 1.0;

    /* generate dark color by cutting brightness down */
    [_colorp getHue: &hue saturation: &sat brightness: &bright alpha: &alpha];
    alpha = 1.0;
    bright = bright * 0.6;
    darkColor = [UIColor colorWithHue: hue saturation: sat brightness: bright alpha: 1.0];

    /* and get the RGB components for the darker color */
    [darkColor getRed: &rgbValues[4] green:&rgbValues[5] blue: &rgbValues[6] alpha: &junk];
    rgbValues[7] = 1.0;

    borderHue = hue + _hueOffset;
    if (borderHue >= 1.0)
	borderHue -= 1.0;
    shadowColor = [UIColor colorWithHue: borderHue saturation: 0.0 brightness: bright alpha: 1.0];

    cx = UIGraphicsGetCurrentContext();

    barFrame = [self frame];
    barFrame.origin.x = 0;
    barFrame.origin.y = 0;

    margin = barFrame.size.height/8;

    if (_noMargin) {
	buttonFrame = barFrame;
    }
    else {
	buttonFrame.origin.x = barFrame.origin.x+margin;
	buttonFrame.origin.y = barFrame.origin.y+margin;
	buttonFrame.size.width = barFrame.size.width - 2*margin;
	buttonFrame.size.height = barFrame.size.height - 2*margin;
    }

    CGContextSaveGState(cx);
    /* erase rectangle so stuff doesn't leak out */
    // CGContextSetFillColorWithColor(cx, [UIColor whiteColor].CGColor);
    // CGContextFillRect(cx, rect);

    /* if this is selected, mark that we're drawing a fairly big shadow, and then
     * draw a rectangle to force the shadow out.
     */
    if (_selected) {
	CGContextSetShadowWithColor(cx, CGSizeMake(0,0), 12.0, shadowColor.CGColor);
	CGContextFillRect(cx, buttonFrame);
    }
    CGContextRestoreGState(cx);

    /* finally draw the glossy button */
    drawGlossy(cx, buttonFrame, rgbValues, 2);
}

- (void) setHueOffset: (float) hueOffset
{
    _hueOffset = hueOffset;
}

@end
