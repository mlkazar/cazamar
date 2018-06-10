//
//  MFANMetalButton.h
//  MusicFan
//
//  Created by Michael Kazar on 4/23/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>

@interface MFANMetalButton : UIButton

- (id)initWithFrame:(CGRect)frame
	      title: (NSString *) title
	      color:(UIColor *) baseColor
	   fontSize: (CGFloat) fontSize;

- (void) addCallback: (id) context
	  withAction: (SEL) callback
	       value: (id) callbackValue;

- (void) setTitle: (NSString *)title;

- (void) setFont: (UIFont *) font;

- (void)drawRect:(CGRect)rect;

- (void) setSelected: (BOOL) isSel;

- (BOOL) isSelected;

- (void) setNoMargin;

@end
