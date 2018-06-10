//
//  MFANTopUpnp.h
//  MusicFan
//
//  Created by Michael Kazar on 4/27/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>

#import "MFANTopApp.h"
#import "MFANMediaSel.h"

@class MFANTopLevel;
@class MFANViewController;
@class MFANSetList;
@class MFANTopUpnp;
@class MFANRadioConsole;

@interface MFANUpnpEdit: NSObject<UITableViewDelegate,
				      UITableViewDataSource>

- (MFANUpnpEdit *) initWithParent:(MFANTopUpnp *) editp
			tableView: (UITableView *) tview
			    probe: (void *) probep
			 tagArray: (NSArray *) tagArray;

@end /* MFANUpnpEdit */

@interface MFANTopUpnp : UIView<UITextViewDelegate, MFANTopApp, MFANAddScan> {
}

@property MFANSetList *setList;

@property BOOL changesMade;

@property BOOL helpMode;

- (id)initWithFrame:(CGRect)frame
     viewController: (MFANViewController *) viewCont;

-(void) activateTop;

-(void) deactivateTop;

- (void) addScanItem: (MFANScanItem *) scan;

- (void *)getDBase;

// - (void) addScanItem: (MFANScanItem *) scan;

+ (UIColor *) backgroundColor;

+ (MFANTopUpnp *) getGlobalUpnp;

- (void) buildTagArray;

- (void) saveStateWithDBase: (bool) saveDBase;

- (void) bkgOp: (id) junk;

@end
