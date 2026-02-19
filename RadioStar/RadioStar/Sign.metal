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

struct SignInfo {
    int selectedId;
};

struct Vertex
{
    float4 position [[position]];
    float4 color;
    float3 normal;
    float2 texturePos;
    uint iid;
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
       device SignInfo *signInfo [[buffer(2)]],
       uint vid [[vertex_id]],
       uint instanceId [[instance_id]])
{
    Vertex vertexOut;
    vertexOut.position = rotations[instanceId].mvpRotationMatrix * vertices[vid].position;
    vertexOut.normal = rotations[instanceId].normalRotationMatrix * vertices[vid].normal;

    float4 selectedGlassColor = float4{0, 0, .6, .25};

    // pass green screen colors through unchanged.  Otherwise, use the
    // incoming glass color unless this is the selected icon.
    if (vertices[vid].color.x == .2 && vertices[vid].color.y == 1.0)
        vertexOut.color = vertices[vid].color;
    else if (instanceId == signInfo->selectedId)
        vertexOut.color = selectedGlassColor;
    else
        vertexOut.color = vertices[vid].color;

    // x goes from -.5 to .5, and y goes from -.3 to .3, and we neeed to map
    // to 0 to 1 in both dimensions.
    vertexOut.iid = instanceId;
    vertexOut.texturePos = float2(vertices[vid].position.x + .5,
    			          (vertices[vid].position.y + .3) / .6);

    return vertexOut;
}

fragment half4 fragment_sign_proc(Vertex vertexIn [[stage_in]],
	 array<texture2d<float>,36> diffuseTextures [[texture(0)]]
)
{
    float4 color;
    float3 lightPosition = {.10, 0, .995};
    float3 lightColor = {0, 1, 0};
    float3 sampleColor;
    float3 diffuseIntensity = saturate(dot(normalize(vertexIn.normal), lightPosition));

    constexpr sampler textureSampler (mag_filter::linear,
                                      min_filter::linear);


    if (vertexIn.color.x == 0.2 && vertexIn.color.y == 1.0) {
        // This is green screen to get image, based on a disgusting version of green
	// that won't be used elsewhere.
	uint iid = vertexIn.iid;
	sampleColor = diffuseTextures[iid].sample(textureSampler, vertexIn.texturePos.xy).rgb;
        color = float4(sampleColor, 1.5);
    } else {
        // point halfway between light source and view direction (Z axis).
        float3 hf = 0.5 *(lightPosition + float3(0, 0, 1));
	float specularFactor = pow(saturate(dot(hf, vertexIn.normal)), 10);
	float3 specular = specularFactor * float3(0, 1, 0);

        color = vertexIn.color +
            float4(diffuseIntensity * lightColor * vertexIn.color.xyz + specular,
                   vertexIn.color.w);
    }

    return half4(color);
}
