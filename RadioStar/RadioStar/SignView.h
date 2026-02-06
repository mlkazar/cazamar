//
//  SignView.h
//  RadioStar
//
//  Created by Michael Kazar on 11/29/25.
//

#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import "ViewController.h"
#import "MFANStreamPlayer.h"

@import simd;

typedef struct _SignVertex {
    vector_float4 _position;	// XYZW
    vector_float4 _color;	// RGBA
    vector_float3 _normal;	// XYZ
    vector_float2 _texturePos;	// XY
} SignVertex;

typedef uint16_t SignIndex;

typedef struct _SignRotation {
    matrix_float4x4 _mvpRotation;
    matrix_float4x4 _mvRotation;
    matrix_float3x3 _normalRotation;
} SignRotations;

typedef struct _SignCoord {
    uint8_t _x;
    uint8_t _y;
} SignCoord;

NS_ASSUME_NONNULL_BEGIN

@interface SignStation : NSObject

@property NSString *stationName;
@property NSString *shortDescr;
@property NSString *streamUrl;
@property NSString *iconUrl;
@property SignCoord rowColumn;
@property bool isPlaying;
@property bool isRecording;
@property CGPoint origin;

- (SignStation *) init;

@end

@interface SignView : UIView

- (SignView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *)vc;

+ (Class) layerClass;

- (CALayer *) makeBackingLayer;

- (void) setSongCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (void) setStateCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (MFANStreamPlayer *) getCurrentPlayer;

@end

NS_ASSUME_NONNULL_END
