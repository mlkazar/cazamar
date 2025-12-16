//
//  GraphTest.m
//  RadioStar
//
//  Created by Michael Kazar on 11/29/25.
//

@import Metal;
@import QuartzCore.CAMetalLayer;

#import "GraphTest.h"

NS_ASSUME_NONNULL_BEGIN

@implementation GraphTest {
    id<MTLDevice> _device;
    CAMetalLayer *_metalLayer;
    id<MTLLibrary> _library;
    id<MTLCommandQueue> _comQueue;
    id<MTLBuffer> _vertexBuffer;
    id<MTLBuffer> _rotationBuffer;
    id<MTLFunction> _vertexProc;
    id<MTLFunction> _fragmentProc;
    id<MTLRenderPipelineState> _pipeline;

    CADisplayLink *_displayLink;

    float _rotationRadians;
}

static matrix_float4x4 rotationMatrix(float radians, float aspect) {
    float sinValue = sinf(radians);
    float cosValue = cosf(radians);

    matrix_float4x4 rval = {
	.columns[0] = {cosValue, sinValue * aspect, 0, 0},
	.columns[1] = {-sinValue, cosValue * aspect, 0, 0},
	.columns[2] = {0, 0, 1, 0},
	.columns[3] = {0, 0, 0, 1}};

    return rval;
}

- (void) setupTriangleBuffer {
    static const GraphVertex vertices[] = {
	{._position = {0, 0.8, 0, 1}, ._color = {1, 0, 0, 1} },
	{._position = {-0.4, 0, 0, 1}, ._color = { 0, 1, 0, 1} },
	{._position = {0.4, 0, 0, 1}, ._color = {0, 0, 1, 1} } };

    _vertexBuffer = [_device newBufferWithBytes: vertices
					 length: sizeof(vertices)
					options: MTLResourceCPUCacheModeDefaultCache];
}

- (void) getRotationBuffer: (id<MTLBuffer>) buffer radians:(float) radians aspect:(float) aspect {
    void *data;
    matrix_float4x4 rotation;

    data = [buffer contents];
    rotation = rotationMatrix(radians, aspect);
    memcpy(data, &rotation, sizeof(RotationMatrix));
}

- (void) setupRotationBuffer {
    _rotationRadians = 0.0;
    _rotationBuffer = [_device newBufferWithLength:sizeof(RotationMatrix)
					   options:MTLResourceCPUCacheModeDefaultCache];
    // Not worth computing aspect just for the very first 60th of a second
    [self getRotationBuffer:_rotationBuffer radians:_rotationRadians aspect: 1.0];
};

- (void) setupPipeline {
    MTLRenderPipelineDescriptor *descr = [MTLRenderPipelineDescriptor new];
    descr.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descr.vertexFunction = _vertexProc;
    descr.fragmentFunction = _fragmentProc;

    NSError *error = nil;
    _pipeline = [_device newRenderPipelineStateWithDescriptor: descr
							error:&error];

    if (_pipeline) {
	_comQueue = [_device newCommandQueue];
    } else {
	NSLog(@"couldn't create pipeline object");
    }
 }

- (void) redraw {
    id<CAMetalDrawable> drawable = [_metalLayer nextDrawable];
    id<MTLTexture> texture;

    if (drawable != nil) {
	CGSize drawableSize = _metalLayer.drawableSize;

	// compute factor to multiply Y coordinate by
	float aspect = drawableSize.width / drawableSize.height;

	[self getRotationBuffer:_rotationBuffer radians:_rotationRadians aspect: aspect];
	_rotationRadians += 0.03;

	// if we're visible, get the frame buffer (called a texture for some
	// reason).
	texture = drawable.texture;

	MTLRenderPassDescriptor *descr = [MTLRenderPassDescriptor renderPassDescriptor];
	descr.colorAttachments[0].texture = texture;
	descr.colorAttachments[0].clearColor = MTLClearColorMake(.9, .9, 1, 1);
	descr.colorAttachments[0].storeAction = MTLStoreActionStore;
	descr.colorAttachments[0].loadAction = MTLLoadActionClear;

	// a buffer for sending commands into a queue for the device
	id<MTLCommandBuffer> comBuffer = [_comQueue commandBuffer];

	// encoder knows how to marshal commands into the comBuffer.
	id<MTLRenderCommandEncoder> encoder =
	    [comBuffer renderCommandEncoderWithDescriptor:descr];
	[encoder setRenderPipelineState: _pipeline];

	// offset is byte offset into vertex buffer's data.  Not sure
	// what atIndex is.
	[encoder setVertexBuffer: _vertexBuffer offset:0 atIndex: 0];
	[encoder setVertexBuffer: _rotationBuffer offset:0 atIndex: 1];

	// tell it to draw trianges
	[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart: 0 vertexCount: 3];

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

- (GraphTest *) initWithFrame: (CGRect) frame ViewCont: (ViewController *)vc {
    self = [super initWithFrame: frame];
    if (self != nil) {
	self.backgroundColor = [UIColor redColor];
	self.frame = frame;

	// Create the device and a metal CA layer
	_device = MTLCreateSystemDefaultDevice();
	_metalLayer = [CAMetalLayer layer];
	_metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	_metalLayer.frame = self.frame;

	[self.layer addSublayer: _metalLayer];

	_library = [_device newDefaultLibrary];

	_vertexProc = [_library newFunctionWithName: @"vertex_proc"];
	_fragmentProc = [_library newFunctionWithName: @"fragment_proc"];

	NSLog(@"vproc %p fproc %p", _vertexProc, _fragmentProc);

	[self setupTriangleBuffer];

	[self setupRotationBuffer];

	[self setupPipeline];
    }

    return self;
}
@end

NS_ASSUME_NONNULL_END
