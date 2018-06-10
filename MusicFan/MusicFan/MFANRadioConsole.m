//
//  MFANRadioConsole.m
//  DJ To Go
//
//  Created by Michael Kazar on 8/1/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANRadioConsole.h"
#import "MFANMetalButton.h"

@implementation MFANRadioConsole {
    NSMutableArray *_radioButtons;
    int _nbuttons;
    int _selectedIx;
    id _context;
    SEL _callback;
    CGRect _frame;
}

CGFloat _4buttonHues[] = {0, 0.17, 0.5, 0.70};

/* note that we don't copy contexts pointer, so target array has to have
 * lifetime longer than the radioConsole.
 */
- (id)initWithFrame: (CGRect)frame
	buttonCount: (int) nbuttons
{
    int i;
    CGFloat buttonGap;
    CGFloat buttonWidth;
    CGFloat appWidth;
    CGRect buttonRect;
    CGFloat hue;
    MFANMetalButton *rbutton;

    self = [super initWithFrame:frame];
    if (self) {
	_radioButtons = [NSMutableArray array];
	_nbuttons = nbuttons;
	_selectedIx = -1;	/* noone selected */
	_frame = frame;
	appWidth = _frame.size.width;

	buttonGap = appWidth / 8 / _nbuttons;
	buttonWidth = (appWidth - (_nbuttons-1) * buttonGap) /_nbuttons;

	/* these components are the same for all buttons */
	buttonRect.origin.y = 0;
	buttonRect.size.height = _frame.size.height;
	buttonRect.size.width = buttonWidth;
	for(i=0;i<_nbuttons;i++) {
	    buttonRect.origin.x = i*(buttonWidth + buttonGap);

	    if (_nbuttons == 4) {
		hue = _4buttonHues[i];
	    }
	    else {
		hue = (float) i/_nbuttons;
	    }

	    rbutton = [[MFANMetalButton alloc] initWithFrame: buttonRect
					       title: nil
					       color: [UIColor colorWithHue: hue
							       saturation: 1.0
							       brightness: 1.0
							       alpha: 1.0]
					       fontSize: 0];

	    [_radioButtons addObject: rbutton];

	    [rbutton addCallback: self
		     withAction: @selector(buttonPressed:)
		     value: [NSNumber numberWithInteger: i]];

	    [self addSubview: rbutton];
	}
    }
    return self;
}

- (void) buttonPressed: (id) whichButtonOb
{
    int ix;

    ix = (int) [whichButtonOb integerValue];
    [self setSelected: ix];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    [_context performSelector: _callback withObject: whichButtonOb];
#pragma clang diagnostic pop
}

- (void) addCallback: (id) target action: (SEL) sel
{
    _context = target;
    _callback = sel;
}

- (void) setSelected: (int) ix
{
    MFANMetalButton *rbutton;

    if (ix >= _nbuttons)
	return;

    if (_selectedIx >= 0) {
	rbutton = [_radioButtons objectAtIndex: _selectedIx];
	[rbutton setSelected: NO];
    }

    _selectedIx = ix;
    if (_selectedIx >= 0) {
	rbutton = [_radioButtons objectAtIndex: _selectedIx];
	[rbutton setSelected: YES]; 
    }
}

- (int) selected
{
    return _selectedIx;
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
