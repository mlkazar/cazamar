//
//  MFANTopEdit.h
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
@class MFANTopEdit;
@class MFANRadioConsole;

#define MFANTopEdit_keepMusic	1
#define MFANTopEdit_keepRadio	2
#define MFANTopEdit_keepPodcast	4

@interface MFANListEdit: NSObject<UITableViewDelegate,
				      UITableViewDataSource>

- (MFANListEdit *) initWithParent:(MFANTopEdit *) edit frame: (CGRect) tframe;

@end /* MFANListEdit */

@interface MFANTopEdit : UIView<UITextViewDelegate, MFANTopApp, MFANAddScan> {
}

@property MFANSetList *setList;

@property BOOL changesMade;

@property BOOL helpMode;

- (id)initWithFrame:(CGRect)frame
	channelType: (MFANChannelType) channelType
	      level: (MFANTopLevel *) topLevel
	    console: (MFANRadioConsole *) console
     viewController: (MFANViewController *) viewCont;

-(void) activateTop;

-(void) deactivateTop;

// - (void) addScanItem: (MFANScanItem *) scan;

+ (UIColor *) backgroundColor;

- (void) setBridgeCallback: (id) obj withAction: (SEL) callback;

- (NSMutableArray *) scanItems;

- (void) pushPopupMedia: (id<MFANMediaSel>) media;

- (void) bridgePopupMedia: (id<MFANMediaSel>) media;

- (void) bridgeAddPressed;

- (void) addPressed: (id) sender withData: (NSNumber *) number;

- (void) pruneScanItems: (int) preserveType;

- (void) setScanFilter: (int) keepType;
@end
