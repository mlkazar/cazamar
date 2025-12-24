//
//  GraphView.m
//  RadioStar
//
//  Created by Michael Kazar on 11/29/25.
//

@import Metal;
@import QuartzCore.CAMetalLayer;
@import simd;

#import "GraphMath.h"
#import "GraphView.h"

NS_ASSUME_NONNULL_BEGIN

@implementation GraphView {
    id<MTLDevice> _device;
    CAMetalLayer *_metalLayer;
    id<MTLLibrary> _library;
    id<MTLCommandQueue> _comQueue;
    id<MTLBuffer> _vertexBuffer;
    id<MTLBuffer> _rotationBuffer;
    id<MTLBuffer> _indexBuffer;
    id<MTLFunction> _vertexProc;
    id<MTLFunction> _fragmentProc;
    id<MTLRenderPipelineState> _pipeline;

    CADisplayLink *_displayLink;

    float _rotationRadians;

    id<MTLTexture> _depthTexture;
    id<MTLDepthStencilState> _depthStencil;
}

static matrix_float4x4 matrixRotateAndTranslate(float radians, CGPoint origin) {
    vector_float3 axis = {.7071, 0, .7071};

    // rotate around (1, 0, 1, 0) (normalized)
#if 1
    matrix_float4x4 rotation = matrix_float4x4_rotation(axis, radians);
#else
    float sinValue = sinf(radians);
    float cosValue = cosf(radians);

    matrix_float4x4 rotation = {
	.columns[0] = {cosValue / 2 + 0.5, .7071*sinValue, 0.5 - 0.5*cosValue},
	.columns[1] = {-.7071*sinValue, cosValue, .7071*sinValue},
	.columns[2] = {0.5 - cosValue/2.0, -.7071*sinValue, cosValue/2.0 + 0.5},
	.columns[3] = {0, 0, 0, 1}};
#endif

    matrix_float4x4 shrinkY = {
	.columns[0] = {1, 0, 0, 0},
	.columns[1] = {0, 1, 0, 0},
	.columns[2] = {0, 0, 1, 0},
	.columns[3] = {origin.x, origin.y, 0, 1}};

    matrix_float4x4 rval = simd_mul(shrinkY, rotation);

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

- (void) setupVertexBuffer {
    static const GraphVertex vertices[] = {
	{._position = {0, 0.8, 0, 1}, ._color = {1, 1, 0, 1.0} },	// 0: top
	{._position = {0, 0, 0.6, 1}, ._color = { 0, 1, 0, 1.0} },	// 1: close closest
	{._position = {0.6, 0, 0, 1},. _color = { 0, 1, 0, 1.0} },	// 2: right bottom
	{._position = {0, 0, -0.6, 1}, ._color = { 0, 0, 1, 1.0} },	// 3: back
	{._position = {-0.60, 0, 0, 1}, ._color = { 0, 0, 1, 1.0} } };	// 4: left bottom

	_vertexBuffer = [_device newBufferWithBytes: vertices
					     length: sizeof(vertices)
					    options: MTLResourceCPUCacheModeDefaultCache];
}

// A triangle with a square base.  One corner facing us, one facing
// away, one to the left and one to the right.
- (void) setupIndexBuffer {
    static const GraphIndex indices[] =
	{0, 1, 4,	// front left triangle
	 0, 4, 3,	// back left triangle
	 0, 3, 2,	// back right triangle
	 0, 2, 1,	// front right triangle
	 3, 4, 1,	// left base triangle
	 1, 2, 3,	// right base triangle
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
    ShaderRotations shaderRotations;

    data = ((char *) [buffer contents]) + (ix * sizeof(ShaderRotations));
    rotation = matrixRotateAndTranslate(radians, origin);

    vector_float3 cameraPosition = {0,0,-3};
    matrix_float4x4 cameraMatrix = matrix_float4x4_translation(cameraPosition);

    shaderRotations._mvRotation = matrix_multiply(cameraMatrix, rotation);

    matrix_float4x4 perspectiveMatrix =
	matrix_float4x4_perspective(aspect, M_PI/2, 1, 64);

    shaderRotations._mvpRotation = matrix_multiply(perspectiveMatrix,
						   shaderRotations._mvRotation);

    memcpy(data, &shaderRotations, sizeof(shaderRotations));
}

- (void) setupRotationBuffer {
    _rotationRadians = 0.0;
    _rotationBuffer = [_device newBufferWithLength:sizeof(ShaderRotations)
					   options:MTLResourceCPUCacheModeDefaultCache];
};

- (void) setupPipeline {
    MTLRenderPipelineDescriptor *descr = [MTLRenderPipelineDescriptor new];
    descr.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descr.vertexFunction = _vertexProc;
    descr.fragmentFunction = _fragmentProc;
    descr.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

    MTLDepthStencilDescriptor *stencilDescr = [MTLDepthStencilDescriptor new];
    stencilDescr.depthCompareFunction = MTLCompareFunctionLess;
    stencilDescr.depthWriteEnabled = YES;
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

    // During the first layout pass, we will not be in a view
    // hierarchy, so we guess our scale
    CGFloat scale = [UIScreen mainScreen].scale;
    
    // If we've moved to a window by the time our frame is being set,
    // we can take its scale as our own
    if (self.window) {
        scale = self.window.screen.scale;
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

	_rotationRadians += 0.01;

	CGPoint origin;
	origin.x = 0.6;
	origin.y = 0.6;
	[self getRotationBuffer:_rotationBuffer
			  index: 0
			radians:_rotationRadians
			 origin: origin
			 aspect: aspect];

	origin.x = -0.6;
	origin.y = -0.6;
	[self getRotationBuffer:_rotationBuffer
			  index: 1
			radians:3*_rotationRadians
			 origin: origin
			 aspect: aspect];

	// if we're visible, get the frame buffer (called a texture for some
	// reason).
	texture = drawable.texture;

	MTLRenderPassDescriptor *descr = [MTLRenderPassDescriptor renderPassDescriptor];
	descr.colorAttachments[0].texture = texture;
	descr.colorAttachments[0].clearColor = MTLClearColorMake(.9, .9, 1, 1);
	descr.colorAttachments[0].storeAction = MTLStoreActionStore;
	descr.colorAttachments[0].loadAction = MTLLoadActionClear;

	descr.depthAttachment.texture = _depthTexture;
	descr.depthAttachment.clearDepth = 1.0;
	descr.depthAttachment.loadAction = MTLLoadActionClear;
	descr.depthAttachment.storeAction = MTLStoreActionDontCare;


	// a buffer for sending commands into a queue for the device
	id<MTLCommandBuffer> comBuffer = [_comQueue commandBuffer];

	// encoder knows how to marshal commands into the comBuffer.
	id<MTLRenderCommandEncoder> encoder =
	    [comBuffer renderCommandEncoderWithDescriptor:descr];
	[encoder setRenderPipelineState: _pipeline];

	// offset is byte offset into vertex buffer's data.  atIndex
	// is used to find the buffer (they're assigned indices at
	// allocation time).
	[encoder setVertexBuffer: _vertexBuffer offset:0 atIndex: 0];

	[encoder setVertexBuffer: _rotationBuffer offset:0 atIndex: 1];

	[encoder setCullMode:MTLCullModeBack];
	[encoder setDepthStencilState: _depthStencil];

	// tell it to draw trianges
	// [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart: 0 vertexCount: 3];
	[encoder drawIndexedPrimitives: MTLPrimitiveTypeTriangle
			    indexCount:[_indexBuffer length] / sizeof(GraphIndex)
			     indexType: MTLIndexTypeUInt16
			   indexBuffer: _indexBuffer
		     indexBufferOffset: 0
			 instanceCount: 2];

	// all done encoding command
        [encoder endEncoding];

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

- (GraphView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *)vc {
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

	_vertexProc = [_library newFunctionWithName: @"vertex_proc"];
	_fragmentProc = [_library newFunctionWithName: @"fragment_proc"];

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
