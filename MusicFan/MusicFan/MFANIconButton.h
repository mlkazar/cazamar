//
//  MFANIconButton.h
//  MusicFan
//
//  Created by Michael Kazar on 4/23/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>

@interface MFANIconButton : UIButton

/* name "context" already taken */

- (id)initWithFrame:(CGRect)frame
	      title: (NSString *) title
	      color:(UIColor *) baseColor
	       file: (NSString *) iconFile;

- (void) addCallback: (id) contextp
	  withAction: (SEL) callback;

- (void) addCallbackContext: (id) context;

- (void) setTitle: (NSString *)title;

- (void) setClearText: (NSString *)text;

- (void) setSelectedInfo: (float) flashTime color: (UIColor *)flashColor;

- (void) setSelectedTitle: (NSString *) selTitle;

- (void) setSelected: (BOOL) selected;

- (NSString *) title;

@end
