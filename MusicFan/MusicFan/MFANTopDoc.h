//
//  MFANTopEdit.h
//  MusicFan
//
//  Created by Michael Kazar on 4/27/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MFANTopApp.h"

@class MFANTopLevel;
@class MFANViewController;
@class MFANTopDoc;

@interface MFANFileItem : NSObject

- (MFANFileItem *) initWithName: (NSString *) name size: (uint64_t) size;

- (NSString *) name;

- (uint64_t) size;

@end

@interface MFANRenamePrompt : UIView<UITextFieldDelegate>

- (MFANRenamePrompt *) initWithFrame: (CGRect) appFrame
			    keyLabel: (NSString *) keyLabel;

- (void) displayFor: (UIView *) parentView sel: (SEL) returnSel parent: (UIView *) parent;

- (NSString *) text;

- (UITextField *) keyView;
@end

@interface MFANDocEdit : UIView<UITableViewDelegate, UITableViewDataSource>
- (MFANDocEdit *) initWithParent:(MFANTopDoc *) edit frame: (CGRect) frame;

- (void) reloadData;

@end

@interface MFANTopDoc : UIView<MFANTopApp>

- (id)initWithFrame:(CGRect)frame
	      level: (MFANTopLevel *) topLevel
     viewController: (MFANViewController *) viewCont;

/* returns array of MFANFileItem objects */
- (NSMutableArray *) fileItems;

- (MFANTopLevel *) topLevel;
@end
