//
//  SignView.m
//  RadioStar
//
//  Created by Michael Kazar on 12/31/25.
//
//  Includes licensed material from Wayne Moore.  See license file.
//

@import Metal;
@import QuartzCore.CAMetalLayer;
@import simd;

#import "GraphMath.h"
#import "SignView.h"

NS_ASSUME_NONNULL_BEGIN

@implementation SignView {
    id<MTLDevice> _device;
    CAMetalLayer *_metalLayer;
    uint8_t *_imageData;
    id<MTLLibrary> _library;
    id<MTLTexture> _wyepTexture;
    id<MTLCommandQueue> _comQueue;
    id<MTLBuffer> _vertexBuffer;
    id<MTLBuffer> _rotationBuffer;
    id<MTLBuffer> _indexBuffer;
    id<MTLFunction> _vertexProc;
    id<MTLFunction> _fragmentProc;
    id<MTLRenderPipelineState> _pipeline;
    CADisplayLink *_displayLink;

    float _rotationRadians;
    bool _rotationDir;

    id<MTLTexture> _depthTexture;
    id<MTLDepthStencilState> _depthStencil;
}

static matrix_float4x4 matrixRotateAndTranslate(float radians, CGPoint origin) {
    //    vector_float3 axis = {.7071, 0, .7071};
    vector_float3 axis={0, 1, 0};

    // rotate around (1, 0, 1, 0) (normalized)
    matrix_float4x4 rotation = matrix_float4x4_rotation(axis, radians);

    matrix_float4x4 moveXY = {
	.columns[0] = {1, 0, 0, 0},
	.columns[1] = {0, 1, 0, 0},
	.columns[2] = {0, 0, 1, 0},
	.columns[3] = {origin.x, origin.y, 0, 1}};

    matrix_float4x4 rval = simd_mul(moveXY, rotation);

    return rval;
}

- (void)setupDepthTexture
{
    CGSize drawableSize = _metalLayer.drawableSize;

    if ( _depthTexture == nil || ([_depthTexture width] != drawableSize.width ||
				  [_depthTexture height] != drawableSize.height)) {
        MTLTextureDescriptor *desc =
	    [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
							       width:drawableSize.width
							      height:drawableSize.height
							   mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget;
        desc.storageMode = MTLStorageModePrivate;
        
        _depthTexture = [_device newTextureWithDescriptor:desc];
    }
}

// Replicate vertices for each face, so we can pass per-face normals
// through the vertex structure.  The bottom is a square positioned so
// that one corner is closest to us, one is furthest, one is on the
// left and one is on the right.  Each triangle's vertices are
// enumerated clockwise as seen from outside the object.
- (void) setupVertexBuffer {
/*
  // Rectangle positions, 1/6 as deep as wide, .6 as high as wide.
  // Total of 6 sides or 12 triangles.
  //
  // Front/back planes at z=.0833 and z=-.0833, x=.5 thru -.5, y=.3 thru -.3
  //
  // Top/bottom at y=.3 & -.3, z and x ranges above.
  //
  // Left/right at x=.5 & -.5, y and z ranges above.
*/
    static const float FZ=.1;		//front z
    static const float BZ=-.1;		//back z
    static const float LX = -0.7;	// left x
    static const float RX = 0.7;	//right x
    static const float TY = 0.42;	// top y
    static const float BY = -0.42;	// bottom y

    static const float SA = .25;	// side alpha
    static const float FA = 0.10;	// front alpha

    static const float GLev = 0.6;	// green level for border

    // Should be 6 sides, 12 triangles, 36 vertices,
    // but we want the side panels to look the same no matter
    // which side they're viewed on, so we end up adding a
    // second winding to be visible from the other side.
    static const SignVertex vertices[] = {
	// back side, top left triangle (same as above, only different
	// Z, winding order and normal.  Color is green screen, so
	// must match .metal file.
	{._position = {LX, BY, BZ, 1}, ._color={.2, 1, 0, 1}, ._normal = {0, 0, -1} },
	{._position = {LX, TY, BZ, 1}, ._color={.2, 1, 0, 1}, ._normal = {0, 0, -1} },
	{._position = {RX, TY, BZ, 1}, ._color={.2, 1, 0, 1}, ._normal = {0, 0, -1} },

	// back side, bottom right triangle
	{._position = {RX, TY, BZ, 1}, ._color={.2, 1, 0, 1}, ._normal = {0, 0, -1} },
	{._position = {RX, BY, BZ, 1}, ._color={.2, 1, 0, 1}, ._normal = {0, 0, -1} },
	{._position = {LX, BY, BZ, 1}, ._color={.2, 1, 0, 1}, ._normal = {0, 0, -1} },

	// left side, top back triangle
	{._position = {LX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {1, 0, 0} },
	{._position = {LX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {1, 0, 0} },
	{._position = {LX, TY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {1, 0, 0} },

	// left side, bottom front triangle
	{._position = {LX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {1, 0, 0} },
	{._position = {LX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {1, 0, 0} },
	{._position = {LX, BY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {1, 0, 0} },

	// left side, top back triangle / alt winding
	{._position = {LX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {-1, 0, 0} },
	{._position = {LX, TY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {-1, 0, 0} },
	{._position = {LX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {-1, 0, 0} },

	// left side, bottom front triangle / alt winding
	{._position = {LX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {-1, 0, 0} },
	{._position = {LX, BY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {-1, 0, 0} },
	{._position = {LX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {-1, 0, 0} },

	// right side, top back triangle (same as left, but diff x,
	// winding and normal)
	{._position = {RX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {-1, 0, 0} },
	{._position = {RX, TY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {-1, 0, 0} },
	{._position = {RX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {-1, 0, 0} },

	// right side, bottom front triangle
	{._position = {RX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {-1, 0, 0} },
	{._position = {RX, BY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {-1, 0, 0} },
	{._position = {RX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {-1, 0, 0} },

	// right side, top back triangle (same as left, but diff x,
	// winding and normal) / alt winding
	{._position = {RX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {1, 0, 0} },
	{._position = {RX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {1, 0, 0} },
	{._position = {RX, TY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {1, 0, 0} },

	// right side, bottom front triangle / alt winding
	{._position = {RX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {1, 0, 0} },
	{._position = {RX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {1, 0, 0} },
	{._position = {RX, BY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {1, 0, 0} },

	// top side, front right triangle
	{._position = {LX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, -1, 0} },
	{._position = {RX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, -1, 0} },
	{._position = {RX, TY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, -1, 0} },

	// top side, back left triangle
	{._position = {RX, TY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, -1, 0} },
	{._position = {LX, TY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, -1, 0} },
	{._position = {LX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, -1, 0} },

	// top side, front right triangle / alt winding
	{._position = {LX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, 1, 0} },
	{._position = {RX, TY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, 1, 0} },
	{._position = {RX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, 1, 0} },

	// top side, back left triangle / alt winding
	{._position = {RX, TY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, 1, 0} },
	{._position = {LX, TY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, 1, 0} },
	{._position = {LX, TY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, 1, 0} },

	// bottom side, front right triangle
	{._position = {LX, BY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, 1, 0} },
	{._position = {RX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, 1, 0} },
	{._position = {RX, BY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, 1, 0} },

	// bottom side, back left triangle
	{._position = {RX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, 1, 0} },
	{._position = {LX, BY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, 1, 0} },
	{._position = {LX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, 1, 0} },

	// bottom side, front right triangle / alt winding
	{._position = {LX, BY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, -1, 0} },
	{._position = {RX, BY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, -1, 0} },
	{._position = {RX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, -1, 0} },

	// bottom side, back left triangle / alt winding
	{._position = {RX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, -1, 0} },
	{._position = {LX, BY, BZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, -1, 0} },
	{._position = {LX, BY, FZ, 1}, ._color={0, GLev, 0, SA}, ._normal = {0, -1, 0} },

	// front side, top left triangle
	{._position = {LX, BY, FZ, 1}, ._color={.9, .9, 1, FA}, ._normal = {0, 0, 1} },
	{._position = {LX, TY, FZ, 1}, ._color={.9, .9, 1, FA}, ._normal = {0, 0, 1} },
	{._position = {RX, TY, FZ, 1}, ._color={.9, .9, 1, FA}, ._normal = {0, 0, 1} },

	// front side, bottom right triangle
	{._position = {RX, TY, FZ, 1}, ._color={.9, .9, 1, FA}, ._normal = {0, 0, 1} },
	{._position = {RX, BY, FZ, 1}, ._color={.9, .9, 1, FA}, ._normal = {0, 0, 1} },
	{._position = {LX, BY, FZ, 1}, ._color={.9, .9, 1, FA}, ._normal = {0, 0, 1} },
    };

    _vertexBuffer = [_device newBufferWithBytes: vertices
					 length: sizeof(vertices)
					options: MTLResourceCPUCacheModeDefaultCache];
}

// A triangle with a square base.  One corner facing us, one facing
// away, one to the left and one to the right.  Since each face has a
// different normal, and we can't pass in a normal through the index
// structure (it's just an integer index), we just replicate the vertices, once for
// each triangle it's part of.
- (void) setupIndexBuffer {
    static const SignIndex indices[] =
	{
	    0, 1, 2,
	    3, 4, 5,
	    6, 7, 8,
	    9, 10, 11,
	    12, 13, 14,
	    15, 16, 17,
	    18, 19, 20,
	    21, 22, 23,
	    24, 25, 26,
	    27, 28, 29,
	    30, 31, 32,
	    33, 34, 35,
	    36, 37, 38,
	    39, 40, 41,
	    42, 43, 44,
	    45, 46, 47,
	    48, 49, 50,
	    51, 52, 53,
	    54, 55, 56,
	    57, 58, 59
	};

    _indexBuffer = [_device newBufferWithBytes: indices
					length: sizeof(indices)
				       options: MTLResourceCPUCacheModeDefaultCache];
};

- (void) getRotationBuffer: (id<MTLBuffer>) buffer
		     index: (uint32_t) ix
		   radians:(float) radians
		    origin: (CGPoint) origin
		    aspect: (float) aspect {
    void *data;
    matrix_float4x4 rotation;
    SignRotations shaderRotations;

    data = ((char *) [buffer contents]) + (ix * sizeof(SignRotations));
    rotation = matrixRotateAndTranslate(radians, origin);

    vector_float3 cameraPosition = {0,0,-5};
    matrix_float4x4 cameraMatrix = matrix_float4x4_translation(cameraPosition);

    shaderRotations._mvRotation = matrix_multiply(cameraMatrix, rotation);

    matrix_float4x4 perspectiveMatrix =
	matrix_float4x4_perspective(aspect, M_PI/2, 1, 64);

    shaderRotations._mvpRotation = matrix_multiply(perspectiveMatrix,
						   shaderRotations._mvRotation);

    shaderRotations._normalRotation = matrix_float4x4_extract_linear
	(shaderRotations._mvRotation);

    memcpy(data, &shaderRotations, sizeof(shaderRotations));
}

- (void) setupRotationBuffer {
    _rotationRadians = -1.0;
    _rotationDir = YES;	// add with time
    _rotationBuffer = [_device newBufferWithLength:sizeof(SignRotations)
					   options:MTLResourceCPUCacheModeDefaultCache];
};

- (void) setupPipeline {
    MTLRenderPipelineDescriptor *descr = [MTLRenderPipelineDescriptor new];
    MTLRenderPipelineColorAttachmentDescriptor *cdescr = descr.colorAttachments[0];
    descr.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descr.vertexFunction = _vertexProc;
    descr.fragmentFunction = _fragmentProc;
    descr.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    cdescr.blendingEnabled = YES;
    cdescr.rgbBlendOperation = MTLBlendOperationAdd;
    cdescr.alphaBlendOperation = MTLBlendOperationAdd;
    cdescr.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    cdescr.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    cdescr.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    cdescr.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    MTLDepthStencilDescriptor *stencilDescr = [MTLDepthStencilDescriptor new];
    stencilDescr.depthCompareFunction = MTLCompareFunctionLess;
    stencilDescr.depthWriteEnabled = NO;
    _depthStencil = [_device newDepthStencilStateWithDescriptor:stencilDescr];

    NSError *error = nil;
    _pipeline = [_device newRenderPipelineStateWithDescriptor: descr
							error:&error];

    if (_pipeline) {
	_comQueue = [_device newCommandQueue];
    } else {
	NSLog(@"couldn't create pipeline object");
    }
 }

- (void)setFrame:(CGRect)frame
{
    [super setFrame:frame];
    
    // setup device here, which is called from 'super initWithFrame' init
    // our initWithFrame function.
    if (_device == nil)
	_device = MTLCreateSystemDefaultDevice();

    CGFloat scale;
    // If we've moved to a window by the time our frame is being set,
    // we can take its scale as our own
    if (self.window) {
        scale = self.window.screen.scale;
    } else {
	scale = 1.0;
    }
    
    CGSize drawableSize = self.bounds.size;
    
    // Since drawable size is in pixels, we need to multiply by the
    // scale to move from points to pixels
    drawableSize.width *= scale;
    drawableSize.height *= scale;

    if (_metalLayer == nil)
	_metalLayer = [CAMetalLayer layer];
    _metalLayer.drawableSize = drawableSize;

    [self setupDepthTexture];
}

- (void) redraw {
    id<CAMetalDrawable> drawable = [_metalLayer nextDrawable];
    id<MTLTexture> texture;

    if (drawable != nil) {
	CGSize drawableSize = _metalLayer.drawableSize;

	// compute factor to multiply Y coordinate by
	float aspect = drawableSize.width / drawableSize.height;

#if 0
	if (_rotationDir) {
	    _rotationRadians += 0.012;
	    if (_rotationRadians > .8)
		_rotationDir = NO;
	} else {
	    _rotationRadians -= 0.012;
	    if (_rotationRadians < -0.8)
		_rotationDir = YES;
	}
#else
	_rotationRadians = 0.0;
#endif

	CGPoint origin;
	origin.x = 1.8;
	origin.y = 4.0;
	[self getRotationBuffer:_rotationBuffer
			  index: 0
			radians: _rotationRadians
			 origin: origin
			 aspect: aspect];

	origin.x = -1.0;
	origin.y = -4.0;
	[self getRotationBuffer:_rotationBuffer
			  index: 1
			radians: _rotationRadians
			 origin: origin
			 aspect: aspect];

	// if we're visible, get the frame buffer (called a texture for some
	// reason).
	texture = drawable.texture;

	MTLRenderPassDescriptor *descr = [MTLRenderPassDescriptor renderPassDescriptor];
	descr.colorAttachments[0].texture = texture;
	descr.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, .2, 1);
	descr.colorAttachments[0].storeAction = MTLStoreActionStore;
	descr.colorAttachments[0].loadAction = MTLLoadActionClear;

	descr.depthAttachment.texture = _depthTexture;
	descr.depthAttachment.clearDepth = 1.0;
	descr.depthAttachment.loadAction = MTLLoadActionClear;
	descr.depthAttachment.storeAction = MTLStoreActionDontCare;


	// a buffer for sending commands into a queue for the device
	id<MTLCommandBuffer> comBuffer = [_comQueue commandBuffer];

	// backEncoder knows how to marshal commands into the comBuffer.
	id<MTLRenderCommandEncoder> backEncoder =
	    [comBuffer renderCommandEncoderWithDescriptor:descr];
	[backEncoder setRenderPipelineState: _pipeline];

	// offset is byte offset into vertex buffer's data.  atIndex
	// is used to find the buffer (they're assigned indices at
	// allocation time).
	[backEncoder setVertexBuffer: _vertexBuffer offset:0 atIndex: 0];

	[backEncoder setVertexBuffer: _rotationBuffer offset:0 atIndex: 1];

	[backEncoder setCullMode:MTLCullModeBack];
	[backEncoder setDepthStencilState: _depthStencil];

	// start drawing back surface
	[backEncoder setFragmentTexture:_wyepTexture atIndex: 0];

#if 0
	[backEncoder drawIndexedPrimitives: MTLPrimitiveTypeTriangle
				indexCount: 6
				 indexType: MTLIndexTypeUInt16
			       indexBuffer: _indexBuffer
			 indexBufferOffset: 0
			     instanceCount: 1];

	// now draw the rest of the box
	[backEncoder drawIndexedPrimitives: MTLPrimitiveTypeTriangle
				indexCount:[_indexBuffer length] / sizeof(SignIndex) - 6
				 indexType: MTLIndexTypeUInt16
			       indexBuffer: _indexBuffer
			 indexBufferOffset: 6 * sizeof(SignIndex)
			     instanceCount: 1];
#else
	// draw the whole box; encoder may do the triangles in order
	// listed.
	[backEncoder drawIndexedPrimitives: MTLPrimitiveTypeTriangle
				indexCount:[_indexBuffer length] / sizeof(SignIndex)
				 indexType: MTLIndexTypeUInt16
			       indexBuffer: _indexBuffer
			 indexBufferOffset: 0
			     instanceCount: 2];
#endif

	// all done encoding command
        [backEncoder endEncoding];

	// now execute the buffer
        [comBuffer presentDrawable:drawable];
        [comBuffer commit];
    }
}

- (void) displayLinkFired: (CADisplayLink *) displayLink {
    [self redraw];
}

// start the drawing pipeline here
- (void) didMoveToSuperview {
    // always run the superclass's code
    [super didMoveToSuperview];

    NSLog(@"in didMoveToSuperview");
    if (self.superview) {
        _displayLink = [CADisplayLink displayLinkWithTarget:self
						   selector:@selector(displayLinkFired:)];
        [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
	NSLog(@"in didMoveToSuperview link=%p", _displayLink);
    } else {
	NSLog(@"didmovetosuperview shutdown");
        [_displayLink invalidate];
        _displayLink = nil;
    }
}

- (id<MTLTexture>) setupTextureFromImage:(UIImage *)image {
    // prepare CG reference
    CGImageRef cgRef = [image CGImage];
    CGColorSpaceRef colorRef = CGColorSpaceCreateDeviceRGB();
    long height = CGImageGetHeight(cgRef);
    long width = CGImageGetWidth(cgRef);
    NSLog(@"cgref=%@ h=%ld w=%ld", cgRef, height, width);

    uint8_t *rawData = (uint8_t *)malloc(height * width * 4);
    long bytesPerPixel = 4;
    long bytesPerRow = bytesPerPixel * width;
    long bitsPerComponent = 8;

    // prepare to draw into bitmap memory
    CGContextRef context = CGBitmapContextCreate
	(rawData, width, height,
	 bitsPerComponent, bytesPerRow, colorRef,
	 kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colorRef);

    // Flip the context so the positive Y axis points down
    CGContextTranslateCTM(context, 0, height);
    CGContextScaleCTM(context, 1, -1);

    // Draw into the bitmap
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), cgRef);
    CGContextRelease(context);

    MTLTextureDescriptor *tdesc = [MTLTextureDescriptor
				      texture2DDescriptorWithPixelFormat:
					  MTLPixelFormatRGBA8Unorm
								   width:width
								  height:height
							       mipmapped:YES];

    id<MTLTexture> texture = [_device newTextureWithDescriptor: tdesc];
    MTLRegion tregion = MTLRegionMake2D(0, 0, width, height);
    [texture replaceRegion: tregion mipmapLevel:0 withBytes: rawData
	       bytesPerRow: bytesPerRow];

    return texture;
}

- (SignView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *)vc {
    self = [super initWithFrame: frame];
    if (self != nil) {
	self.backgroundColor = [UIColor redColor];
	self.frame = frame;

	// Create the device and a metal CA layer
	assert(_device != nil);
	if (_metalLayer == nil)
	    _metalLayer = [CAMetalLayer layer];
	_metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	_metalLayer.frame = self.frame;

	[self.layer addSublayer: _metalLayer];

	_library = [_device newDefaultLibrary];

	_vertexProc = [_library newFunctionWithName: @"vertex_sign_proc"];
	_fragmentProc = [_library newFunctionWithName: @"fragment_sign_proc"];

	// load image from resource
	UIImage *image = [UIImage imageNamed: @"wyep.jpeg"];
	_wyepTexture = [self setupTextureFromImage: image];;

	[self setupDepthTexture];

	[self setupVertexBuffer];

	[self setupIndexBuffer];

	[self setupRotationBuffer];

	[self setupPipeline];
    }

    return self;
}
@end

NS_ASSUME_NONNULL_END
