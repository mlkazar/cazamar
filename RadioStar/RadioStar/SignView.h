//
//  SignView.h
//  RadioStar
//
//  Created by Michael Kazar on 11/29/25.
//

#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import "ViewController.h"

@import simd;

typedef struct _SignVertex {
    vector_float4 _position;	// XYZW
    vector_float4 _color;	// RGBA
    vector_float3 _normal;	// XYZ
    vector_float2 _texturePos;	// XY
} SignVertex;

typedef uint16_t SignIndex;

typedef struct _SignRotations {
    matrix_float4x4 _mvpRotation;
    matrix_float4x4 _mvRotation;
    matrix_float3x3 _normalRotation;
} SignRotations;

NS_ASSUME_NONNULL_BEGIN

@interface SignView : UIView

- (SignView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *)vc;

@end

NS_ASSUME_NONNULL_END
