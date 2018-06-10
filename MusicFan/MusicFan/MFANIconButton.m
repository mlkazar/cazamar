//
//  MFANIconButton.m
//  MusicFan
//
//  Created by Michael Kazar on 4/23/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANIconButton.h"
#import "MFANCGUtil.h"
#import "MFANTopSettings.h"

@implementation MFANIconButton {
    SEL _callback;
    id __weak _context;	/* pointer up the hierarchy would prevent refcount GC */
    id __weak _context2;
    BOOL _selected;
    BOOL _flashLit;
    NSTimer *_flashTimer;
    float _flashTime;
    UIColor *_color;
    CGRect _frame;
    NSString *_title;
    NSString *_selectedTitle;
    UIImage *_settingsImage;
    UIImage *_silverImage;
    UIImage *_selectedImage;
}

- (BOOL) selected
{
    return _selected;
}

- (void) setTitle: (NSString *)title
{
    _title = title;
    [self setNeedsDisplay];
}

- (void) updateTitle: (NSString *) title
{
    [self setTitle: title forState: UIControlStateNormal];
    [self setTitle: title forState: UIControlStateSelected];
}

- (void) setClearText: (NSString *)title
{
    _title = title;
    [self updateTitle: title];
}

- (NSString *) title
{
    return _title;
}

- (id)initWithFrame:(CGRect)frame
	      title: (NSString *) title
	      color:(UIColor *) baseColor
	       file: (NSString *) iconFile
{
    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	self.backgroundColor = [UIColor clearColor];
	[self setTitleColor: baseColor forState: UIControlStateNormal];
	[self setTitleColor: baseColor forState: UIControlStateSelected];
	self.titleLabel.font = [UIFont boldSystemFontOfSize: frame.size.height*0.15];
	// [self.titleLabel setTextAlignment: NSTextAlignmentRight];
	[self setContentVerticalAlignment:UIControlContentVerticalAlignmentBottom];
	[self setContentHorizontalAlignment:UIControlContentHorizontalAlignmentCenter];
	_color = baseColor;
	_selected = NO;
	_frame = frame;
	[self setClearText: title];
	_selectedTitle = title;
	_callback = nil;
	_context = nil;
	_context2 = nil;

	/* flashing stuff */
	_flashTimer = nil;
	_flashLit = NO;
	_flashTime = 1.0;

	_silverImage = [UIImage imageNamed: iconFile];
	//_silverImage = resizeImage( [UIImage imageNamed: @"button-red.png"],
	//frame.size.height);
	_selectedImage = _silverImage;

	[self addTarget: self
	      action: @selector(buttonPressed:withEvent:)
	      forControlEvents: UIControlEventAllTouchEvents];
    }
    return self;
}

- (void) setSelectedInfo: (float) flashTime color: (UIColor *)flashColor;
{
    _selectedImage = tintImage(_silverImage, flashColor);
    _flashTime = flashTime;
}

- (void) setSelected: (BOOL) selected;
{
    _selected = selected;
    if (selected) {
	if (!_flashTimer) {
	    _flashTimer = [NSTimer scheduledTimerWithTimeInterval: _flashTime
				   target:self
				   selector:@selector(flashTimerFired:)
				   userInfo:nil
				   repeats: YES];
	    _flashLit = YES;
	}
	[self updateTitle: _selectedTitle];
    }
    else {
	/* not selected, stop timer */
	if (_flashTimer != nil) {
	    [_flashTimer invalidate];
	    _flashTimer = nil;
	}
	_flashLit = NO;
	[self updateTitle: _title];
    }
    [self setNeedsDisplay];
}

- (void) setSelectedTitle: (NSString *) selTitle
{
    _selectedTitle = selTitle;
    [self setNeedsDisplay];
}

- (void) flashTimerFired: (id) junk
{
    if (!_selected) {
	[_flashTimer invalidate];
	_flashTimer = nil;
	return;
    }

    if (_flashLit)
	_flashLit = NO;
    else
	_flashLit = YES;

    [self setNeedsDisplay];
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
    CGContextRef cx;
    CGRect newRect;

    cx = UIGraphicsGetCurrentContext();

    /* finally draw the glossy button */
    newRect = rect;
    newRect.size.height = rect.size.height * 0.85;
    newRect.size.width = newRect.size.height;
    newRect.origin.x = rect.size.width/2 - newRect.size.width/2;
    if (_flashLit) {
	[_selectedImage drawInRect: newRect];
    }
    else {
	[_silverImage drawInRect: newRect];
    }
}

@end
