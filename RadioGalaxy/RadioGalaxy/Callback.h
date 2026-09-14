#import <UIKit/UIKit.h>

@interface CallbackEntry : NSObject

@property NSObject *callbackObj;
@property SEL callbackSel;

- (CallbackEntry *) initWithObj: (NSObject *) obj sel:(SEL) sel;
@end

@interface CallbackSet : NSObject

- (CallbackSet *) init;

- (void) addCallbackWithObj: (NSObject *) obj
			sel: (SEL) sel;

- (int32_t) removeCallbackWithObj: (NSObject *) obj
			      sel: (SEL) sel;

- (void) shutdown;

- (bool) haveCallbacks;

- (void) applyWithParm: (NSObject *) parm;

@end
