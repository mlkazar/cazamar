//
//  MFANWarn.h
//  DJ To Go
//
//  Created by Michael Kazar on 2/7/15.
//  Copyright (c) 2015 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>

@interface MFANWarn : UIView

- (MFANWarn *) initWithTitle: (NSString *) title
		     message: (NSString *) message
			secs: (float) secs;
@end
