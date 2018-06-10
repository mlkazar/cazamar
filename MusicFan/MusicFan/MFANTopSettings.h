//
//  MFANTopSettings.h
//  MusicFan
//
//  Created by Michael Kazar on 11/287/15.
//  Copyright (c) 2015 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MFANTopApp.h"

// static const int MFANTopSettings_forceNoArt = 1;
static const int MFANTopSettings_forceNoArt = 0;

@class MFANViewController;

@interface MFANLabel : UIButton

- (MFANLabel *) initWithFrame: (CGRect) frame
		       target: (id) target
		     selector: (SEL) selector;


- (MFANLabel *) initWithTarget: (id) target selector:(SEL) selector;
@end

@interface MFANTopRivo : UIView<MFANTopApp>
- (MFANTopRivo *) initWithFrame: (CGRect) frame viewController: (MFANViewController *) vc;
@end

@interface MFANTopSettings : UIView<MFANTopApp>

#define MFANTopSettings_warnedTop	0x40
#define MFANTopSettings_warnedCloud	0x10
#define MFANTopSettings_warnedUpnp	0x20
#define MFANTopSettings_initialResync	0x80

/* must include all settings that are saved */
#define MFANTopSettings_warnedAll	(0xF0)	/* keep updated! */

- (MFANTopSettings *) initWithFrame: (CGRect) frame viewController: (MFANViewController *) vc;

- (void) helpRandomizeMusic: (id) junk withData: (id) junk2;

- (void) helpUseCloudDefault: (id) junk withData: (id) junk2;

- (void) helpResyncLibrary: (id) junk withData: (id) junk2;

- (void) helpSendUsage: (id) junk withData: (id) junk2;

-(void) activateTop;

-(void) deactivateTop;

+ (int) randomizePodcasts;

+ (int) randomizeMusic;

+ (int) useCloudDefault;

+ (int) licensed;

+ (int) sendUsage;

+ (int) autoDim;

+ (int) autoDownload;

+ (unsigned int) firstUsed;

+ (UIFont *) basicFontWithSize: (CGFloat) size;

+ (int) useCellForDownloads;

+ (int) neverUseCellData;

+ (int) warningFlags;

+ (void) setWarningFlag: (int) flag;

+ (int) unloadPlayed;

+ (UIColor *) baseColor;

+ (UIColor *) lightBaseColor;

+ (UIColor *) clearBaseColor;

+ (UIColor *) textColor;

+ (MFANTopSettings *) globalSettings;

@end
