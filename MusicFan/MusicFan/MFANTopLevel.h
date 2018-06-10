//
//  MFANTopLevel.h
//  MusicFan
//
//  Created by Michael Kazar on 4/20/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>

#import "MFANTopApp.h"
#import "MFANPlayContext.h"
#import "MFANRadioConsole.h"
#import "MFANComm.h"

@class MFANViewController;
@class MFANSetList;

@interface MFANAutoDim : NSObject

- (MFANAutoDim *) init;

- (void) enableTimer;

- (void) disableTimer;

- (void) timerFired: (id) junk;

@end

@interface MFANTopLevel : UIView<UITextFieldDelegate,MFANTopApp>

@property (strong, nonatomic) UITextField *promptView;

- (void) helpPressed: (id) arg;

- (id)initWithFrame:(CGRect)frame
     viewController:(MFANViewController *) viewCont
	       comm: (MFANComm *) comm;

- (void) buttonPressed: (id) sender;

- (MFANSetList *) setList;

- (MFANRadioConsole *) radioConsole;

- (void) playCurrent;

- (void) enterBackground;

- (void) leaveBackground;

- (void) activateTop;

- (void) deactivateTop;

- (MFANPlayContext *) currentContext;

- (void) setCurrentContext: (MFANPlayContext *) newContext;

- (MFANPlayContext *) popupContext;

- (void) recordingStopped;

- (MFANPlayerView *) playerView;

- (MFANPlayContext *) getContextAtIndex: (int) ix;

- (BOOL) hasAnyItems;

- (NSArray *) getPlayContexts;

@end
