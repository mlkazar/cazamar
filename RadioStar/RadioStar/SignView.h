//
//  SignView.h
//  RadioStar
//
//  Created by Michael Kazar on 11/29/25.
//

#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import "RadioHistory.h"
#import "ViewController.h"
#import "MFANStreamPlayer.h"
#import "SignStation.h"

NS_ASSUME_NONNULL_BEGIN

@interface SignView : UIView

@property RadioHistory *history;
@property ViewController *vc;
@property (readonly) MFANStreamPlayer *player;
@property BOOL resumeAtEnd;

- (SignView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *)vc;

+ (Class) layerClass;

- (CALayer *) makeBackingLayer;

- (void) setSongCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (void) setStateCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (NSString *) getPlayingStationName;

- (MFANStreamPlayer *) getCurrentPlayer;

- (MFANAqStream *) getCurrentStream;

- (SignStation *) getCurrentStation;

- (void) setRadioHistory: (RadioHistory *) history;

- (void) seek: (float) distance;

- (void) stopRadioResetStream: (BOOL) doReset;

- (void) changeStationBy: (int16_t) change;

- (void) startCurrentStation;

- (void) displayAppOptions;

- (void) stopRecording;

- (void) startRecording;
@end

NS_ASSUME_NONNULL_END
