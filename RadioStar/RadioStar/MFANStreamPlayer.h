#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

#import "MFANAqStream.h"

NSString *MFANStreamPlayer_getUnknownString(void);

@interface MFANStreamPlayer : NSObject

- (MFANStreamPlayer *) initWithStream : (MFANAqStream *) stream;

- (void) resume;

- (void) pause;

- (NSString *)getCurrentPlaying;

- (BOOL) isPlaying;

- (void) setStateCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (void) setSongCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (void) shutdown;

- (bool) isShutdown;

- (NSString *) getEncodingType;

- (NSString *) getStreamUrl;

- (float) dataRate;

@end
