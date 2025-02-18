//
//  MFANCoreButton.m
//  MusicFan
//
//  Created by Michael Kazar on 4/23/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANCoreButton.h"
#import "MFANCGUtil.h"
#import "MFANTopSettings.h"

@implementation MFANCoreButton {
    SEL _callback;
    id __weak _context;	/* pointer up the hierarchy would prevent refcount GC */
    id __weak _context2;
    int _selected;
    UIColor *_color;
    UIColor *_fillColor;
    CGRect _frame;
    NSString *_title;
    UIImage *_settingsImage;
    UIImage *_silverImage;
}

- (BOOL) selected
{
    return _selected;
}

- (void) setSelected: (BOOL) selected
{
    CGFloat hue;
    CGFloat sat;
    CGFloat bright;
    CGFloat alpha;

    _selected = selected;
    [_color getHue: &hue saturation: &sat brightness: &bright alpha: &alpha];
    if (selected)
	sat = 1.0;
    else
	sat = 0.3;
    _color = [[UIColor alloc] initWithHue: hue saturation: sat brightness: bright alpha: alpha];

    [self setNeedsDisplay];
}

- (void) setTitle: (NSString *)title
{
    _title = title;
    [self setNeedsDisplay];
}

- (void) setClearText: (NSString *)title
{
    [self setTitle: title forState: UIControlStateNormal];
    [self setTitle: title forState: UIControlStateSelected];
}

- (NSString *) title
{
    return _title;
}

- (id)initWithFrame:(CGRect)frame
	      title: (NSString *) title
	      color:(UIColor *) baseColor
{
    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	self.backgroundColor = [UIColor clearColor];
	[self setTitleColor: baseColor forState: UIControlStateNormal];
	[self setTitleColor: baseColor forState: UIControlStateSelected];
	self.titleLabel.font = [UIFont boldSystemFontOfSize: frame.size.height*0.6];
	[self.titleLabel setTextAlignment: NSTextAlignmentCenter];
	_color = baseColor;
	_fillColor = [UIColor clearColor];
	_selected = YES;
	_frame = frame;
	_title = title;
	_callback = nil;
	_context = nil;
	_context2 = nil;

	_silverImage = resizeImage2( [UIImage imageNamed: @"button.png"],
				     frame.size);

	if ([title isEqualToString: @"Settings"]) {
	    _settingsImage = resizeImage( [UIImage imageNamed: @"settings.png"],
					  frame.size.width);
	    //_settingsImage = tintImage(_settingsImage, baseColor);
	    _settingsImage = traceImage(_silverImage, _settingsImage);
	    [self setImage: _settingsImage forState: UIControlStateNormal];
	}

	[self addTarget: self
	      action: @selector(buttonPressed:withEvent:)
	      forControlEvents: UIControlEventAllTouchEvents];
    }
    return self;
}

- (void) setFillColor: (UIColor *) fillColor
{
    _fillColor = fillColor;
}

- (void) addCallback: (id) contextp
	  withAction: (SEL) callback
{
    _callback = callback;
    _context = contextp;
}

- (void) addCallbackContext: (id) context2
{
    _context2 = context2;
}

- (void) buttonPressed: (id) button withEvent: (UIEvent *) event
{
    NSSet *allTouches;
    NSEnumerator *en;
    UITouch *touch;
    UITouchPhase phase;

    if (![event respondsToSelector:@selector(allTouches)]) {
	/* BOGUS event */
	NSLog(@"! Button bogus event");
	return;
    }

    allTouches = [event allTouches];
    en = [allTouches objectEnumerator];
    phase = UITouchPhaseBegan;
    while( touch = [en nextObject]) {
	phase = [touch phase];
    }

    if (phase == UITouchPhaseBegan) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
	NSLog(@"- Button press sent on phase=%d", (int) phase);
	[_context performSelector: _callback withObject: _context2];
#pragma clang diagnostic pop
    }
    else {
	NSLog(@"- Button press event ignored phase=%d", (int) phase);
    }
}

// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
- (void)drawRect:(CGRect)rect
{
    CGFloat ydim;
    CGFloat xdim;
    CGFloat yorigin;
    CGFloat xorigin;
    CGContextRef cx;
    UIImage *outlineImage;
    UIImage *finishedImage;

    // Drawing code
    if ([_title isEqual:@"Circle"]) {
	/* erase whole rectangle so that old junk doesn't show through */
	UIGraphicsBeginImageContext(rect.size);
	cx = UIGraphicsGetCurrentContext();
	CGContextSetFillColorWithColor(cx, [UIColor clearColor].CGColor);
	CGContextFillRect(cx, rect);

	/* note must reduce radius if increase line width */
	CGContextAddArc(cx,
			/* center.x */ rect.origin.x+rect.size.width/2,
			/* center.y */ rect.origin.y + rect.size.height/2,
			/* radius */ rect.size.height/2 - 2,
			/* start angle */0,
			/* end angle */ 2 * M_PI,
			/* clockwise */ 1);

	CGContextSetLineWidth(cx, 3.0);
	CGContextSetFillColorWithColor(cx, [UIColor clearColor].CGColor);
	CGContextSetStrokeColorWithColor(cx, _color.CGColor);
	CGContextDrawPath(cx, kCGPathFillStroke);
	outlineImage = UIGraphicsGetImageFromCurrentImageContext();
	UIGraphicsEndImageContext();
	
	finishedImage = traceImage(_silverImage, outlineImage);
	cx = UIGraphicsGetCurrentContext();
	[finishedImage drawInRect: rect];
    }
    if ([_title isEqual:@"None"]) {
	/* erase whole rectangle so that old junk doesn't show through */
	cx = UIGraphicsGetCurrentContext();
	CGContextSetFillColorWithColor(cx, [UIColor clearColor].CGColor);
	CGContextFillRect(cx, rect);
	CGContextDrawPath(cx, kCGPathFillStroke);
    }
    else if ([_title isEqual:@"Border"]) {
	UIBezierPath *bezierPath;
	CGFloat strokeWidth = 2.0;
	CGRect insetRect;

	/* erase whole rectangle so that old junk doesn't show through */
	cx = UIGraphicsGetCurrentContext();
	CGContextSetFillColorWithColor(cx, _fillColor.CGColor);
	CGContextFillRect(cx, rect);

	insetRect = CGRectInset(rect, strokeWidth, strokeWidth);
	self.titleEdgeInsets = UIEdgeInsetsMake(strokeWidth,
						strokeWidth,
						strokeWidth,
						strokeWidth);
	CGContextSetLineWidth(cx, strokeWidth);
	CGContextSetFillColorWithColor(cx, [MFANTopSettings clearBaseColor].CGColor);
	CGContextSetStrokeColorWithColor(cx, _color.CGColor);
	bezierPath = [UIBezierPath bezierPathWithRoundedRect: insetRect
				   cornerRadius:  2*strokeWidth];
	// CGContextDrawPath(cx, kCGPathFillStroke);
	[bezierPath fill];
	[bezierPath stroke];
    }
    else if ([_title isEqual:@"Settings"]) {
	/* erase whole rectangle so that old junk doesn't show through */
	cx = UIGraphicsGetCurrentContext();
	CGContextSetFillColorWithColor(cx, [UIColor clearColor].CGColor);
	CGContextFillRect(cx, rect);
	CGContextDrawPath(cx, kCGPathFillStroke);
    }
    else if ([_title isEqual:@"Play"]) {
	cx = UIGraphicsGetCurrentContext();
	ydim = 0.8*rect.size.height;
	xdim = ydim * 0.4*1.732;	/* for 60 degrees */
	yorigin = rect.origin.y+(rect.size.height-ydim)/2;
	xorigin = rect.origin.x+(rect.size.width-xdim)/2;
	CGContextBeginPath(cx);
	CGContextMoveToPoint( cx, xorigin, yorigin);
	CGContextAddLineToPoint( cx, xorigin+xdim, yorigin+ydim/2);
	CGContextAddLineToPoint( cx, xorigin, yorigin+ydim);
	CGContextClosePath(cx);
	CGContextSetFillColorWithColor(cx, [_color CGColor]);
	CGContextFillPath(cx);
    }
    else if ([_title isEqual:@"Pause"]) {
	cx = UIGraphicsGetCurrentContext();
	ydim = 0.8*rect.size.height;
	xdim = ydim * 0.4;
	yorigin = rect.origin.y+(rect.size.height-ydim)/2;
	xorigin = rect.origin.x+(rect.size.width-xdim)/2;

	CGContextBeginPath(cx);
	CGContextSetLineWidth(cx, 4.0);
	CGContextSetStrokeColorWithColor(cx, [_color CGColor]);
	CGContextMoveToPoint( cx, xorigin, yorigin);
	CGContextAddLineToPoint( cx, xorigin, yorigin+ydim);
	CGContextStrokePath(cx);

	CGContextBeginPath(cx);
	CGContextMoveToPoint( cx, xorigin+xdim, yorigin);
	CGContextAddLineToPoint( cx, xorigin+xdim, yorigin+ydim);
	CGContextStrokePath(cx);
    }
    else if ([_title isEqual:@"Next"]) {
	cx = UIGraphicsGetCurrentContext();
	ydim = 0.6*rect.size.height;
	xdim = ydim*0.4 * 2;
	yorigin = rect.origin.y+(rect.size.height-ydim)/2;
	xorigin = rect.origin.x+(rect.size.width-xdim)/2;

	CGContextBeginPath(cx);
	CGContextSetLineWidth(cx, 4.0);
	CGContextSetStrokeColorWithColor(cx, [_color CGColor]);
	CGContextMoveToPoint( cx, xorigin, yorigin);
	CGContextAddLineToPoint( cx, xorigin+xdim/2, yorigin+ydim/2);
	CGContextAddLineToPoint( cx, xorigin, yorigin+ydim);
	CGContextStrokePath(cx);

	/* now draw same thing offset by xdim/2 */
	CGContextBeginPath(cx);
	CGContextSetStrokeColorWithColor(cx, [_color CGColor]);
	CGContextMoveToPoint( cx, xorigin+xdim/2, yorigin);
	CGContextAddLineToPoint( cx, xorigin+xdim, yorigin+ydim/2);
	CGContextAddLineToPoint( cx, xorigin+xdim/2, yorigin+ydim);
	CGContextStrokePath(cx);
    }
    else if ([_title isEqual:@"Chevron"]) {
	cx = UIGraphicsGetCurrentContext();
	ydim = 0.5*rect.size.height;
	xdim = ydim*0.6;
	yorigin = rect.origin.y+(rect.size.height-ydim)/2;
	xorigin = rect.origin.x+(rect.size.width-xdim)/2;

	CGContextBeginPath(cx);
	CGContextSetLineWidth(cx, 3.0);
	CGContextSetStrokeColorWithColor(cx, [_color CGColor]);
	CGContextMoveToPoint( cx, xorigin, yorigin);
	CGContextAddLineToPoint( cx, xorigin+xdim, yorigin+ydim/2);
	CGContextAddLineToPoint( cx, xorigin, yorigin+ydim);
	CGContextStrokePath(cx);
    }
    else if ([_title isEqual:@"Prev"]) {
	cx = UIGraphicsGetCurrentContext();
	ydim = 0.6*rect.size.height;
	xdim = ydim*0.4 * 2;
	yorigin = rect.origin.y+(rect.size.height-ydim)/2;
	xorigin = rect.origin.x+(rect.size.width-xdim)/2;

	CGContextBeginPath(cx);
	CGContextSetLineWidth(cx, 4.0);
	CGContextSetStrokeColorWithColor(cx, [_color CGColor]);
	CGContextMoveToPoint( cx, xorigin+xdim/2, yorigin);
	CGContextAddLineToPoint( cx, xorigin, yorigin+ydim/2);
	CGContextAddLineToPoint( cx, xorigin+xdim/2, yorigin+ydim);
	CGContextStrokePath(cx);

	/* now draw same thing offset by xdim/2 */
	CGContextBeginPath(cx);
	CGContextSetStrokeColorWithColor(cx, [_color CGColor]);
	CGContextMoveToPoint( cx, xorigin+xdim, yorigin);
	CGContextAddLineToPoint( cx, xorigin+xdim/2, yorigin+ydim/2);
	CGContextAddLineToPoint( cx, xorigin+xdim, yorigin+ydim);
	CGContextStrokePath(cx);
	outlineImage = UIGraphicsGetImageFromCurrentImageContext();
    }
}

@end
