//
//  MFANTopHistory.h
//  MusicFan
//
//  Created by Michael Kazar on 4/27/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>

#import "MFANTopApp.h"

@class MFANViewController;
@class MFANSetList;
@class MFANTopHistory;

@interface MFANHistoryItem : NSObject {
}

@property long when;
@property NSString *song;	/* stream title */
@property NSString *station;

@end

@interface MFANListHistory: NSObject<UITableViewDelegate,
				      UITableViewDataSource>

- (MFANListHistory *) initWithParent:(MFANTopHistory *) editp tableView: (UITableView *) tview;

@end /* MFANListHistory */

@interface MFANTopHistory : UIView<UITextViewDelegate, MFANTopApp> {
}

@property MFANSetList *setList;

@property BOOL changesMade;

@property BOOL helpMode;

- (id)initWithFrame:(CGRect)frame
     viewController: (MFANViewController *) viewCont;

-(void) activateTop;

-(void) deactivateTop;

+ (UIColor *) backgroundColor;

- (NSMutableArray *) histItems;

- (void) addHistoryStation: (NSString *) station withSong: (NSString *) song;

@end
