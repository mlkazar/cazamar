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
#import "MFANAqStream.h"
#import "MFANStreamPlayer.h"
#import "SignView.h"

NS_ASSUME_NONNULL_BEGIN

@implementation SignStation {
    NSString *_stationName;
    NSString *_shortDescr;
    NSString *_streamUrl;
    NSString *_iconUrl;

    // row and column, both 0 based
    SignCoord _rowColumn;
}

- (SignStation *) init {
    self = [super init];
    if (self) {
	self.isPlaying = NO;
	self.isRecording = NO;
    }
    return self;
}
@end

@implementation SignView {
    id<MTLDevice> _device;
    CAMetalLayer *_metalLayer;
    uint8_t *_imageData;
    id<MTLLibrary> _library;
    id<MTLTexture> _wyepTexture;
    id<MTLTexture> _wmfoTexture;
    id<MTLCommandQueue> _comQueue;
    id<MTLBuffer> _vertexBuffer;
    id<MTLBuffer> _rotationBuffer;
    id<MTLBuffer> _indexBuffer;
    id<MTLFunction> _vertexProc;
    id<MTLFunction> _fragmentProc;
    id<MTLRenderPipelineState> _pipeline;
    CADisplayLink *_displayLink;

    NSMutableOrderedSet *_allStations;

    ViewController *_vc;

    // computeLayout figure out how many of each icon fit in the X and
    // Y directions.  The [xy]Spread values are the distance from
    uint32_t _xCount;
    uint32_t _yCount;
    float _xSpread;
    float _ySpread;
    float _xSpace;
    float _ySpace;

    float _rotationRadians;
    bool _rotationDir;

    id<MTLTexture> _depthTexture;
    id<MTLDepthStencilState> _depthStencil;

    SignStation *_playingStation;
    MFANAqStream *_stream;
    MFANStreamPlayer *_player;
}

// some defines for the images we're dealing with
static const float cameraDistance = 5.0;
static const float boundingX = 1.4;
static const float boundingY = 0.6 * boundingX;
static const uint32_t maxIcons = 36;

static matrix_float4x4 matrixRotateAndTranslate(float radians, CGPoint origin) {
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

    if (_depthTexture == nil || ([_depthTexture width] != drawableSize.width ||
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
  // Front/back planes at z=.0833 and z=-.0833, x=.7 thru -.7, y=.5 thru -.5
  //
  // Top/bottom at y=.3 & -.3, z and x ranges above.
  //
  // Left/right at x=.5 & -.5, y and z ranges above.
*/
    static const float FZ=.1;			//front z
    static const float BZ=-.1;			//back z

    static const float LX = -boundingX/2;	// left x
    static const float RX = boundingX/2;	//right x
    static const float TY = boundingY/2;	// top y
    static const float BY = -boundingY/2;	// bottom y

    static const float SA = .25;	// side alpha
    static const float FA = 0.10;	// front alpha

    static const float GLev = 0.6;	// green level for border

    // Should be 6 sides, 12 triangles, 36 vertices,
    // but we want the side panels to look the same no matter
    // which side they're viewed on, so we end up adding a
    // second winding to be visible from the other side.
    static const SignVertex vertices[] = {
	// back side, top left triangle (same as above, only different
	// Z, winding order and normal.  Color is my defined green
	// (.2, 1, 0) screen, so must match .metal file.
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

SignCoord SignCoordMake(uint8_t x,uint8_t y) {
    SignCoord rval;
    rval._x = x;
    rval._y = y;

    return rval;
}

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

    vector_float3 cameraPosition = {0,0,-cameraDistance};
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
    _rotationBuffer = [_device newBufferWithLength:36*sizeof(SignRotations)
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

- (void) computeLayout {
    // Because we use a 90 degree viewing pyramid (frustrum?) to view
    // the screen at a distance of cameraDistance from the object, the
    // Y coordinate in world space goes from -cameraDistance up to
    // +cameraDistance, or with 5 for the camera distance, from -5 to 5.
    //
    // The X coordinate is reduced by the screen frame ratio of
    // width/height.  That's probably about 0.5 on a typical iPhone,
    // and so is about -.2.5 to 2.5.
    //
    float topMargin = 0.2;
    float leftMargin = 0.1;

    _ySpace = 10.0;
    _xSpace = _ySpace * (self.frame.size.width / self.frame.size.height);

    // we leave a little extra space around the icons to make them
    // look OK even with the edge (in the Z direction) potentially
    // blocking the adjacent icon.
    float iconWidth = boundingX + 0.05;
    float iconHeight = boundingY + 0.20;

    // How many can we fit horizontally and vertically
    int32_t xCount = (_xSpace - leftMargin) / iconWidth;
    int32_t yCount = (_ySpace - topMargin) / iconHeight;

    // how much extra space do we have on each line, per icon.  Double left
    // margin so we leave some space on the right as well.x
    float extraX = ((_xSpace - 2*leftMargin) - (xCount * iconWidth)) / xCount;
    if (extraX < 0.0)
	extraX = 0.0;

    NSLog(@"Icon array %d X %d stations extraX=%f", xCount, yCount, extraX);

    //  Note that when we set an origin for an icon, that's the
    // location in world space for the center of the icon.  And note
    // that the world space has (0, 0) in its center.  Note that we
    // may assign too many icons positions on the screen (more than
    // maxIcons), but the render loop will stop adding them to
    // the redraw list if that happens.
    float xPos = (iconWidth - _xSpace) / 2.0 + leftMargin;
    float yPos = (_ySpace - iconHeight) / 2.0 - topMargin;
    uint32_t columns = 0;
    SignStation *station;
    for(station in _allStations) {
	station.origin = CGPointMake(xPos, yPos);
	columns++;
	if (columns >= xCount) {
	    // switch to next row
	    xPos = (iconWidth - _xSpace) / 2.0 + leftMargin;
	    yPos -= iconHeight;
	    columns = 0;
	} else {
	    xPos += iconWidth + extraX;
	}
    }

}

- (void)setFrame:(CGRect)frame
{
    // TODO: frame height is too large by origin.y
    frame.size.height -= frame.origin.y;
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
    NSLog(@"frame updated to %f x %f at %f.%f", frame.size.width, frame.size.height,
	  frame.origin.x, frame.origin.y);
    NSLog(@"bounds updated to %f x %f", self.bounds.size.width, self.bounds.size.height);
    
    // Since drawable size is in pixels, we need to multiply by the
    // scale to move from points to pixels
    drawableSize.width *= scale;
    drawableSize.height *= scale;

    if (_metalLayer == nil)
	_metalLayer = [CAMetalLayer layer];
    _metalLayer.drawableSize = drawableSize;

    [self setupDepthTexture];
}

- (void) addStation: (NSString *) stationName
	 shortDescr: (NSString *) shortDescr
	  streamUrl: (NSString *) streamUrl
	    iconUrl: (NSString *) iconUrl
	  rowColumn: (SignCoord) rowColumn {
    SignStation *station = [[SignStation alloc] init];
    station.stationName = stationName;
    station.shortDescr = shortDescr;
    station.streamUrl = streamUrl;
    station.iconUrl = iconUrl;
    station.rowColumn = rowColumn;

    [_allStations addObject: station];
}

- (void) redraw {
    id<CAMetalDrawable> drawable = [_metalLayer nextDrawable];
    id<MTLTexture> texture;

    if (drawable != nil ) {
	if ([_allStations count] == 0)
	    return;

	CGSize drawableSize = _metalLayer.drawableSize;

	// compute factor to multiply Y coordinate by
	float aspect = drawableSize.width / drawableSize.height;

	_rotationRadians = 0.0;

	// if we're visible, get the frame buffer (called a texture for some
	// reason).
	texture = drawable.texture;

	MTLRenderPassDescriptor *descr = [MTLRenderPassDescriptor renderPassDescriptor];
	descr.colorAttachments[0].texture = texture;
	descr.colorAttachments[0].clearColor = MTLClearColorMake(.9, .9, 1.0, 1);
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

	SignStation *station;
	uint32_t signCount = 0;
	for(station in _allStations) {
#if 0
	    // assigned by computeLayout now
	    CGPoint origin;
	    origin.x = -1.5 + 1.5* station.rowColumn._x;
	    origin.y = 4.0 - station.rowColumn._y;
#endif

	    if (signCount >= maxIcons)
		break;

	    [self getRotationBuffer:_rotationBuffer
			      index: signCount
			    radians: _rotationRadians
			     origin: station.origin
			     aspect: aspect];

	    [backEncoder setFragmentTexture: [self getTextureForUrl: station.iconUrl]
				    atIndex: signCount];

	    signCount++;
	}

	// offset is byte offset into vertex buffer's data.  atIndex
	// is used to find the buffer (they're assigned indices at
	// allocation time).
	[backEncoder setVertexBuffer: _vertexBuffer offset:0 atIndex: 0];

	[backEncoder setVertexBuffer: _rotationBuffer offset:0 atIndex: 1];

	[backEncoder setCullMode:MTLCullModeBack];
	[backEncoder setDepthStencilState: _depthStencil];

	// start drawing back surface

	// draw the whole box; encoder may do the triangles in order
	// listed.
	[backEncoder drawIndexedPrimitives: MTLPrimitiveTypeTriangle
				indexCount:[_indexBuffer length] / sizeof(SignIndex)
				 indexType: MTLIndexTypeUInt16
			       indexBuffer: _indexBuffer
			 indexBufferOffset: 0
			     instanceCount: signCount];

	// all done encoding command
        [backEncoder endEncoding];

	// now execute the buffer
        [comBuffer presentDrawable:drawable];
        [comBuffer commit];

	// NB: you need to call animationOn to update the
	// metal-controlled layer or to enable real animation, since
	// it burns ~12% of the CPU to just keep running the display
	// link / GPU to keep a fixed image on the screen.
	//
	// Perhaps a better approach is to use a 3D graphics API
	// instead, but using metal allows us to do animation if we
	// want.
	[self animationOff];
    }
}

- (void) displayLinkFired: (CADisplayLink *) displayLink {
    [self redraw];
}

- (void) animationOn {
    if (_displayLink == nil) {
	_displayLink = [CADisplayLink displayLinkWithTarget:self
						   selector:@selector(displayLinkFired:)];
	[_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    }
}

- (void) animationOff {
    if (_displayLink != nil) {
	[_displayLink invalidate];
	_displayLink = nil;
    }
}

// start the drawing pipeline here
- (void) didMoveToSuperview {
    // always run the superclass's code
    [super didMoveToSuperview];

    NSLog(@"in didMoveToSuperview");
    if (self.superview) {
	[self animationOn];
	NSLog(@"in didMoveToSuperview link=%p", _displayLink);
    } else {
	NSLog(@"didmovetosuperview shutdown");
	[self animationOff];
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

- (id<MTLTexture>) getTextureForUrl: (NSString *) imageUrlString
{
    // Load the image
    NSURL *imageUrl = [NSURL URLWithString: imageUrlString];
    NSData *imageData = [[NSData alloc] initWithContentsOfURL: imageUrl];
    UIImage *image = [UIImage imageWithData: imageData];

    // Create a context (canvas) to draw in
    CGSize size = image.size;
    CGRect rect = CGRectMake(0, 0, size.width, size.height);
    UIGraphicsBeginImageContextWithOptions(size, YES, image.scale);

    // fill it with white so that transparent parts of the icon don't
    // appear weird/dark, and then= draw the image of the icon into
    // this current context.
    CGContextRef context = UIGraphicsGetCurrentContext();
    [[UIColor whiteColor] setFill];
    CGContextFillRect(context, rect);
    [image drawInRect: CGRectMake(0, 0, size.width, size.height)];
    UIImage *newImage = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    // turn the image into a metal texture.
    id<MTLTexture> returnTexture;
    returnTexture = [self setupTextureFromImage: newImage];

    return returnTexture;
}

- (SignView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *)vc {
    self = [super initWithFrame: frame];
    if (self != nil) {
	self.backgroundColor = [UIColor clearColor];
	self.frame = frame;

	_vc = vc;
	_allStations = [[NSMutableOrderedSet alloc] init];

	[self addStation: @"WYEP"
	      shortDescr: @"Where music matters"
	       streamUrl: @"https://ais-sa3.cdnstream1.com/2557_128.mp3"
		 iconUrl: @"https://wyep.org/apple-touch-icon.png"
	       rowColumn: SignCoordMake(0,0)];

	[self addStation: @"WESA"
	      shortDescr: @"Where news matters"
	       streamUrl: @"https://ais-sa3.cdnstream1.com/2556_128.mp3"
		 iconUrl: @"https://wesa.org/apple-touch-icon.png"
	       rowColumn: SignCoordMake(1,0)];

	[self addStation: @"WESA"
	      shortDescr: @"Where news matters"
	       streamUrl: @"https://ais-sa3.cdnstream1.com/2556_128.mp3"
		 iconUrl: @"https://wesa.org/apple-touch-icon.png"
	       rowColumn: SignCoordMake(1,1)];

	[self addStation: @"WYEP"
	      shortDescr: @"Where music matters"
	       streamUrl: @"https://ais-sa3.cdnstream1.com/2557_128.mp3"
		 iconUrl: @"https://wyep.org/apple-touch-icon.png"
	       rowColumn: SignCoordMake(2,8)];

	// assign origin points to all stations in world space Note
	// that world space's origin is in the center, and each icon's
	// object's center in object space is at (0,0).
	[self computeLayout];

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
	{
	    NSURL *imageUrl = [NSURL URLWithString:@"https://wyep.org/apple-touch-icon.png"];
	    NSData *imageData = [[NSData alloc] initWithContentsOfURL: imageUrl];
	    UIImage *image = [UIImage imageWithData: imageData];

	    CGSize size = image.size;
	    CGRect rect = CGRectMake(0, 0, size.width, size.height);
	    UIGraphicsBeginImageContextWithOptions(size, YES, image.scale);

	    CGContextRef context = UIGraphicsGetCurrentContext();
	    [[UIColor whiteColor] setFill];
	    CGContextFillRect(context, rect);
	    [image drawInRect: CGRectMake(0, 0, size.width, size.height)];
	    UIImage *newImage = UIGraphicsGetImageFromCurrentImageContext();
	    UIGraphicsEndImageContext();

	    _wyepTexture = [self setupTextureFromImage: newImage];
	}

	UIImage *image = [UIImage imageNamed: @"wmfo.jpg"];
	_wmfoTexture = [self setupTextureFromImage: image];;

	[self setupDepthTexture];

	[self setupVertexBuffer];

	[self setupIndexBuffer];

	[self setupRotationBuffer];

	[self setupPipeline];

	_playingStation = nil;

	UIGestureRecognizer *recog;
	recog = [[UILongPressGestureRecognizer alloc]
		    initWithTarget: self action:@selector(longPressed:)];
	[self addGestureRecognizer: recog];

	recog = [[UITapGestureRecognizer alloc]
		    initWithTarget: self action:@selector(tapPressed:)];
	[self addGestureRecognizer: recog];
    }

    return self;
}

- (void) displayOptions
{
    UIAlertController *alert = [UIAlertController
				   alertControllerWithTitle: @"Test title"
						    message: @"Whole station"
					     preferredStyle: UIAlertControllerStyleAlert];

    UIAlertAction *action = [UIAlertAction actionWithTitle:@"Option 1"
                                                     style: UIAlertActionStyleDefault
                                                   handler:^(UIAlertAction *act) {
            NSLog(@"Perform 1");
	}];
    [alert addAction: action];

    action = [UIAlertAction actionWithTitle:@"Option 2"
                                      style: UIAlertActionStyleDefault
                                    handler:^(UIAlertAction *act) {
	    NSLog(@"Perform 2");
        }];
    [alert addAction: action];

    [_vc presentViewController: alert animated:YES completion: nil];
}

- (void) longPressed: (UILongPressGestureRecognizer *) sender {
    CGPoint point = [sender locationInView:self];
    if (sender.state == UIGestureRecognizerStateBegan) {
	NSLog(@"long pressed at (%f,%f)", point.x, point.y);
	[self displayOptions];
    } else {
	NSLog(@"ignoring begin long press");
    }
}

- (SignStation *) findStationByTouch:(CGPoint) touchPosition
{
    CGSize dims = self.frame.size;
    CGPoint viewOrigin = self.frame.origin;
    float modelX = ((touchPosition.x - viewOrigin.x) / dims.width) * _xSpace - _xSpace/2;
    float modelY = _ySpace/2 - ((touchPosition.y - viewOrigin.y) / dims.height) * _ySpace;

    SignStation *station;
    for(station in _allStations) {
	if ( (modelX >= station.origin.x - boundingX / 2) &&
	     (modelX <= station.origin.x + boundingX / 2) &&
	     (modelY >= station.origin.y - boundingY / 2) &&
	     (modelY <= station.origin.y + boundingY / 2)) {
	    return station;
	}
    }

    return nil;
}

- (void) tapPressed: (UILongPressGestureRecognizer *) sender {
    CGPoint point = [sender locationInView:self];
    if (sender.state == UIGestureRecognizerStateEnded) {
	SignStation *station = [self findStationByTouch: point];
	SignStation *prevStation = _playingStation;
	NSLog(@"tap station=%p name=%@", station, station.stationName);

	// stop playing old station
	if (prevStation != nil) {
	    [self stopRadio];
	    _playingStation = nil;
	}

	// start new station if different.
	if (station != nil && station != prevStation) {
	    _stream = [[MFANAqStream alloc] initWithUrl:station.streamUrl];
	    _player = [[MFANStreamPlayer alloc] initWithStream: _stream];
	    _playingStation = station;
	}
    } else {
	NSLog(@"ignoring begin tap");
    }
}

- (void) stopRadio {
    NSLog(@"in restartradio");
    [_player shutdown];
    NSLog(@"streamplayer shutdown done");
    [_stream shutdown];
    _player = nil;
    _stream = nil;
    NSLog(@"shutdown of mfanaqstream done");
}

@end

NS_ASSUME_NONNULL_END
