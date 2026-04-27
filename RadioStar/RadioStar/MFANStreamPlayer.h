#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

#import "MFANAqStream.h"

NSString *MFANStreamPlayer_getUnknownString(void);

@interface Callback : NSObject
@property NSObject *callbackObj;
@property SEL callbackSel;

- (Callback *) init;

- (Callback *) initWithObj: (NSObject *) obj sel:(SEL) sel;

@end

@interface MFANStreamPlayer : NSObject

@property BOOL muted;

- (MFANStreamPlayer *) initWithStream : (MFANAqStream *) stream ms: (uint64_t) ms;

- (void) resume;

- (void) pause;

- (NSString *)getCurrentPlaying;

- (bool) isPaused;

- (BOOL) isPlaying;

- (void) addStateCallback: (NSObject *) callbackObj  sel: (SEL) callbackSel;

- (void) setSongCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (void) shutdown;

- (bool) isShutdown;

- (float) getDataRate;

- (NSString *) getEncodingType;

- (NSString *) getPublicUrl;

- (uint64_t) getSeekTarget: (float) offset;

- (void) mute;

- (void) unmute;

- (void) setupAudioSession: (BOOL) mix;

@end
