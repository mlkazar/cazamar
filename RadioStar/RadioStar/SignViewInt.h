//
//  SignView.h
//  RadioStar
//
//  Created by Michael Kazar on 03/29/2026
//
// Keep this separate so we can include SignView.h in
// Obj-C++ files (which can't do @import)
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
