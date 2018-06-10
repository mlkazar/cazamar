//
//  MFANRadioConsole.h
//  DJ To Go
//
//  Created by Michael Kazar on 8/1/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>

@interface MFANRadioConsole : UIView

- (id)initWithFrame: (CGRect)frame
	buttonCount: (int) nbuttons;

- (void) addCallback: (id) target action: (SEL) sel;

- (void) setSelected: (int) ix;

- (int) selected;

@end
