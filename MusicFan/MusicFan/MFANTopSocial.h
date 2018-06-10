//
//  MFANTopSocial.h
//  DJ To Go
//
//  Created by Michael Kazar on 12/4/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MFANTopApp.h"
#import "MFANComm.h"
#import "MFANViewController.h"

@interface ListSocialEntry: NSObject
- (ListSocialEntry *) init;

@property NSString *s1;
@property NSString *s2;
@property NSString *s3;

@end

@class MFANViewController;

@interface MFANListSocial: NSObject<UITableViewDelegate,
					UITableViewDataSource>

- (MFANListSocial *) initWithParent: (UIView *) editp
			  tableView: (UITableView *) tview;

@end /* MFANListEdit */

@interface MFANTopSocial : UIView<MFANTopApp>

- (id)initWithFrame:(CGRect)frame
     viewController: (MFANViewController *) viewCont
	       comm: (MFANComm *) comm;

@end
