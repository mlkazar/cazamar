//
//  RadioHistory.h
//  MusicFan
//
//  Created by Michael Kazar on 4/27/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>

@class ViewController;
@class MFANSetList;
@class RadioHistory;

@interface MFANHistoryItem : NSObject {
}

@property long when;
@property NSString *song;	/* stream title */
@property NSString *station;
@property BOOL highlighted;
@end

@interface RadioListHistory: NSObject<UITableViewDelegate,
				      UITableViewDataSource>

- (RadioListHistory *) initWithParent:(RadioHistory *) editp tableView: (UITableView *) tview;

@end /* RadioListHistory */

@interface RadioHistory : UIView<UITextViewDelegate> {
}

@property MFANSetList *setList;

@property BOOL changesMade;

@property BOOL helpMode;

- (id)initWithViewController: (ViewController *) viewCont;

+ (UIColor *) backgroundColor;

- (NSMutableArray *) histItems;

- (void) addHistoryStation: (NSString *) station withSong: (NSString *) song;

- (void) toggleHighlight;

- (BOOL) isHighlighted;

- (void) setCallback: (id) object WithSel: (SEL) selector;

@end
