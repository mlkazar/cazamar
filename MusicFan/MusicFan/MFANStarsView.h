//
//  MFANStarsView.h
//  MusicFan
//
//  Created by Michael Kazar on 5/29/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>

@interface MFANStarsView : UIView

- (void) addCallback: (id) callbackObj
	  withAction: (SEL) callbackSel;

- (void) clearCallback;

- (id)initWithFrame:(CGRect)frame background: (BOOL) background;

@property long enabledCount;

@end
