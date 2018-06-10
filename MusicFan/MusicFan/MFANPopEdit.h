//
//  MFANPopEdit.h
//  MusicFan
//
//  Created by Michael Kazar on 6/5/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MFANMediaSel.h"

@interface MFANPopEditOverride : UIView <UITextFieldDelegate>
- (MFANPopEditOverride *) initWithFrame: (CGRect) appFrame
			       keyLabel: (NSString *) keyLabel
			     valueLabel: (NSString *) valueLabel;

- (void) displayFor: (UIView *) parentView sel: (SEL) returnSel;

@end

@interface MFANPopEdit  : UIView <UITextViewDelegate,
				      UITableViewDelegate,
				      UITableViewDataSource,
				      UISearchBarDelegate> 

- (MFANPopEdit *) initWithFrame: (CGRect) appFrame
		     scanHolder: (id<MFANAddScan>) scanHolder
		       mediaSel: (id<MFANMediaSel>) mediaSel;

- (void) addCallback: (id) callbackObj finishedSel: (SEL) finishedSel nextSel: (SEL) nextSel;

- (void) updatePopViewInfo;

@end
