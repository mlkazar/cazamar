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

@import simd;

typedef struct _SignVertex {
    vector_float4 _position;	// XYZW
    vector_float4 _color;	// RGBA
    vector_float3 _normal;	// XYZ
    vector_float2 _texturePos;	// XY
    unsigned int _iid;
} SignVertex;

typedef struct _SignInfo {
    unsigned int _selectedId;
} SignInfo;

typedef uint16_t SignIndex;

typedef struct _SignRotation {
    matrix_float4x4 _mvpRotation;
    matrix_float4x4 _mvRotation;
    matrix_float3x3 _normalRotation;
} SignRotations;

NS_ASSUME_NONNULL_BEGIN

@interface SignView : UIView

@property RadioHistory *history;
@property ViewController *vc;

- (SignView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *)vc;

+ (Class) layerClass;

- (CALayer *) makeBackingLayer;

- (void) setSongCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (void) setStateCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (NSString *) getPlayingStationName;

- (MFANStreamPlayer *) getCurrentPlayer;

- (void) setRadioHistory: (RadioHistory *) history;

@end

NS_ASSUME_NONNULL_END
