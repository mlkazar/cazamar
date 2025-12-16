/*
The MIT License (MIT)

Copyright (c) 2015 Warren Moore

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <metal_stdlib>

using namespace metal;

struct Vertex
{
    float4 position [[position]];
    float4 color;
};

struct Rotations
{
    float4x4 rotationMatrix;
};


vertex Vertex vertex_proc(const device Vertex *vertices [[buffer(0)]],
       constant Rotations *rotations [[buffer(1)]],
       uint vid [[vertex_id]],
       uint instanceId [[instance_id]])
{
    Vertex vertexOut;
    vertexOut.position = rotations[instanceId].rotationMatrix * vertices[vid].position;
    vertexOut.color = vertices[vid].color;

    return vertexOut;
}

fragment half4 fragment_proc(Vertex vertexIn [[stage_in]])
{
    return half4(vertexIn.color);
}
