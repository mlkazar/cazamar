//
//  MFANAddChoice.m
//  DJ To Go
//
//  Created by Michael Kazar on 8/14/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANAddChoice.h"
#import "MFANRButton.h"
#import "MFANViewController.h"
#import "MFANSetList.h"
#import "MFANPlayContext.h"
#import "MFANIndicator.h"
#import "MFANStarsView.h"
#import "MFANCGUtil.h"
#import "MFANPopEdit.h"
#import "MFANRadioConsole.h"
#import "MFANAddChoice.h"
#import "MFANTopSettings.h"

@implementation MFANAddChoice {
    NSMutableArray *_rbuttonArray;	/* of MFANRButtons */ 
    NSArray *_contexts;			/* of ids from caller */
    NSArray *_labels;			/* of NSStrings from caller */
    int _labelCount;
    int _buttonCount;			/* including cancel button */
    MFANViewController *_viewCon;
    UIColor *_color;
    CGRect _frame;
    SEL _callback;
    id _callbackTarget;
}

- (id)initWithFrame:(CGRect)frame
	     labels: (NSArray *)labels
	   contexts: (NSArray *) contexts
     viewController: (MFANViewController *) viewCon
{
    CGFloat height;
    CGFloat hmargin;
    CGFloat vmargin;
    CGFloat col2Offset;
    int i;
    MFANRButton *rbutton;
    CGRect tframe;
    int rowCount;

    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code
	_frame = frame;
	_labels = labels;
	_contexts = contexts;
	_viewCon = viewCon;

	_labelCount = (int) [labels count];
	_buttonCount = _labelCount+1;
	rowCount = ((_buttonCount + 1) & ~1) / 2;

	_rbuttonArray = [[NSMutableArray alloc] init];

	/* two columns now */
	height = frame.size.height / rowCount;
	vmargin = 2;
	hmargin = 2;
	tframe.origin.x = hmargin;
	tframe.origin.y = vmargin;
	/* we layout one margin, one button, another margin, another
	 * button and the final margin.
	 */
	tframe.size.width = (_frame.size.width - 3*hmargin) / 2;
	tframe.size.height = height-2*vmargin;
	col2Offset = tframe.size.width + 2*hmargin;

	for(i=0;i<_buttonCount;i++) {
	    if (i & 1) {
		/* right hand side */
		tframe.origin.x = col2Offset;
	    }
	    else {
		/* left hand side */
		tframe.origin.x = hmargin;
	    }

	    if (i < _labelCount) {
		/* avoid 0, since that's cancel */
		_color = [UIColor colorWithHue: (float) (i/2 + 1) / (rowCount+1)
				  saturation: 1.0
				  brightness: 1.0
				  alpha: 1.0];

		rbutton = [[MFANRButton alloc] initWithFrame: tframe
					       title: _labels[i]
					       color: _color
					       fontSize: 0];
	    }
	    else {
		/* cancel button */
		rbutton = [[MFANRButton alloc] initWithFrame: tframe
					       title: @"Done"
					       color: [UIColor greenColor]
					       fontSize: 0];
	    }

	    // font choices: Cochin-Bold(1,cute,serif), Copperplate (3, spread),
	    // Didot-Bold (4, serif,light)
	    // HoeflerText-Black(2,serif)
	    [rbutton setNoMargin];
	    [rbutton setFont: [UIFont fontWithName: @"Cochin-Bold"
				       size: tframe.size.height*0.2]];

	    [_rbuttonArray addObject: rbutton];
	    [self addSubview: rbutton];

	    /* after the 2nd button, add in the next */
	    if (i&1)
		tframe.origin.y += height;
	}
    }

    return self;
}

- (void) addCallback: (id) target
	  withAction: (SEL) callback
{
    int i;
    MFANRButton *rbutton;

    for(i=0;i<_buttonCount;i++) {
	rbutton = _rbuttonArray[i];
	if (i < _labelCount) {
	    [rbutton addCallback: target withAction: callback value: _contexts[i]];
	}
	else {
	    /* cancel button is distinguished by nil callback parameter */
	    [rbutton addCallback: target withAction: callback value: nil];
	}
    }
}

// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
/*
- (void)drawRect:(CGRect)rect
{
    // Drawing code
}
*/

@end
