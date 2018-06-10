//
//  MFANIndicator.h
//  MusicFan
//
//  Created by Michael Kazar on 5/11/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>

@interface MFANIndicator : UIView

- (float) progress;

- (void) setProgress: (float) progress;

- (void) setTitle: (NSString *) title;

- (void) drawRect:(CGRect)rect;

- (void) setDone;

@end
