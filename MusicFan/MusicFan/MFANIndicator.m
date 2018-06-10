//
//  MFANIndicator.m
//  MusicFan
//
//  Created by Michael Kazar on 5/11/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANIndicator.h"
#import "MFANCGUtil.h"

@implementation MFANIndicator {
    BOOL _done;
    float _progress;
    NSTimer *_timer;
    CGRect _frame;
    NSString *_title;
    UILabel *_label;
    UIImage *_image;
}

- (void) setTitle: (NSString *) title
{
    [_label setText: title];
}

- (float) progress
{
    return _progress;
}

- (void) setProgress: (float) progress
{
    _progress = progress;
}

- (void) setDone
{
    _done = 1;
}

- (id)initWithFrame:(CGRect)frame
{
    CGRect textFrame;
    float textHeight = 25.0;

    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	_done = NO;
	_progress = 0.0;
	_frame = frame;
	

	_image = [UIImage imageNamed:@"silver.jpg"];

	[self setBackgroundColor: [UIColor whiteColor]];

	textFrame = CGRectMake(_frame.origin.x,
			       (_frame.origin.y +
				_frame.size.height/4),
			       _frame.size.width,
			       textHeight);

	// _label = [[UILabel alloc] initWithFrame: textFrame];
	// [_label setFont: [UIFont systemFontOfSize: textHeight * 0.95]];
	// [_label setTextAlignment: NSTextAlignmentCenter];
	// [_label setText: @"Initializing..."];

	_timer = [NSTimer scheduledTimerWithTimeInterval: 0.3
			  target: self
			  selector: @selector(updateView:)
			  userInfo: nil
			  repeats: NO];
    }
    return self;
}

/* called on main thread to draw current state */
- (void) updateView: (id) context
{
    [self setNeedsDisplay];
    // [_label setNeedsDisplay];

    _timer = nil;
    if (!_done) {
	_timer = [NSTimer scheduledTimerWithTimeInterval: 0.3
			  target: self
			  selector: @selector(updateView:)
			  userInfo: nil
			  repeats: NO];
    }
}

- (void)drawRect:(CGRect)rect
{
    CGContextRef cx;
    CGRect barFrame;
    CGFloat colors[] = {1.0, 0.6, 0.0, 1.0,
			1.0, 0.6, 0.25, 1.0};

    UIColor *shadowColor;

    [_image drawInRect: rect];

    barFrame = CGRectMake(_frame.origin.x,
			  (_frame.origin.y +
			   3*_frame.size.height/4),
			  _frame.size.width * _progress,
			  30);

    cx = UIGraphicsGetCurrentContext();

    shadowColor = [UIColor colorWithRed: 0.4 green: 0.3 blue:0.2 alpha: 1.0];

    CGContextSaveGState(cx);
    CGContextSetShadowWithColor(cx, CGSizeMake(3,5), 5.0, shadowColor.CGColor);
    CGContextSetFillColorWithColor(cx, [UIColor whiteColor].CGColor);
    CGContextFillRect(cx, barFrame);
    CGContextRestoreGState(cx);

    drawGlossy(cx, barFrame, colors, 2);
}

@end
