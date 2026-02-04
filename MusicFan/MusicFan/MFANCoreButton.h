//
//  MFANCoreButton.h
//  MusicFan
//
//  Created by Michael Kazar on 4/23/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>

@interface MFANCoreButton : UIButton

/* name "context" already taken */

- (id)initWithFrame:(CGRect)frame
	      title: (NSString *) title
	      color:(UIColor *) baseColor;

- (id)initWithFrame:(CGRect)frame
	      title: (NSString *) title
	      color:(UIColor *) baseColor
    backgroundColor:(UIColor *) backgroundColor;

- (void) addCallback: (id) contextp
	  withAction: (SEL) callback;

- (void) addCallbackContext: (id) context;

- (void) setTitle: (NSString *)title;

- (void) setClearText: (NSString *)text;

- (void) setFillColor: (UIColor *) fillColor;

- (NSString *) title;

@end
