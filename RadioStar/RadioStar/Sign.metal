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
    float3 normal;
};

struct Rotations
{
    // Basic MVP matrix
    float4x4 mvpRotationMatrix;

    // same as above, without the projection at the end.
    float4x4 mvRotationMatrix;

    // rotation for normal doesn't include translation phase,
    // since normal doesn't change under translation
    float3x3 normalRotationMatrix;
};


vertex Vertex vertex_sign_proc(const device Vertex *vertices [[buffer(0)]],
       constant Rotations *rotations [[buffer(1)]],
       uint vid [[vertex_id]],
       uint instanceId [[instance_id]])
{
    Vertex vertexOut;
    vertexOut.position = rotations[instanceId].mvpRotationMatrix * vertices[vid].position;
    vertexOut.normal = rotations[instanceId].normalRotationMatrix * vertices[vid].normal;
    vertexOut.color = vertices[vid].color;

    return vertexOut;
}

fragment half4 fragment_sign_proc(Vertex vertexIn [[stage_in]])
{
    float4 color;
    float3 lightPosition = {0,1,0};
    float3 lightColor = {1, 1, 1};
    float3 diffuseIntensity = saturate(dot(normalize(vertexIn.normal), lightPosition));
    color = vertexIn.color + float4(diffuseIntensity * lightColor * vertexIn.color.xyz, vertexIn.color.w);

    return half4(color);
}
