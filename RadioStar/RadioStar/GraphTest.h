//
//  GraphTest.h
//  RadioStar
//
//  Created by Michael Kazar on 11/29/25.
//

#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import "ViewController.h"

@import simd;

typedef struct _GraphVertex {
    vector_float4 _position;	// XYZW
    vector_float4 _color;	// RGBA
} GraphVertex;

typedef struct _RotationMatrix {
    matrix_float4x4 _data;
} RotationMatrix;

typedef uint16_t GraphIndex;

NS_ASSUME_NONNULL_BEGIN

@interface GraphTest : UIView

- (GraphTest *) initWithFrame: (CGRect) frame ViewCont: (ViewController *)vc;

@end

NS_ASSUME_NONNULL_END
