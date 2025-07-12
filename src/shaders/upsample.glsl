#include "roundedcorners.glsl"

uniform sampler2D texUnit;
uniform float offset;
uniform vec2 halfpixel;

uniform bool noise;
uniform sampler2D noiseTexture;
uniform vec2 noiseTextureSize;

uniform float edgeSizePixels;
uniform float refractionStrength;
uniform float refractionNormalPow;

varying vec2 uv;

void main(void)
{
    vec2 texSize = 1.0 / halfpixel / 2.0; // approximate texSize from halfpixel
    vec2 fragCoord = uv * texSize;

    float distToEdgeX = min(fragCoord.x, texSize.x - fragCoord.x);
    float distToEdgeY = min(fragCoord.y, texSize.y - fragCoord.y);
    float distToEdge = min(distToEdgeX, distToEdgeY);

    float concaveFactor = pow(clamp(1.0 - distToEdge / edgeSizePixels, 0.0, 1.0), refractionNormalPow);

    float totalDist = distToEdgeX + distToEdgeY;
    float weightX = 1.0 - (distToEdgeX / totalDist); // more weight when closer to vertical edge
    float weightY = 1.0 - (distToEdgeY / totalDist); // more weight when closer to horizontal edge

    float dirX = (fragCoord.x < texSize.x * 0.5) ? 1.0 : -1.0;
    float dirY = (fragCoord.y < texSize.y * 0.5) ? 1.0 : -1.0;

    vec2 blendedNormal = normalize(vec2(dirX * weightX, dirY * weightY));

    vec3 normal = normalize(vec3(blendedNormal, concaveFactor));

    float finalStrength = 0.05 * concaveFactor * refractionStrength;

    // Different refraction offsets for each color channel
    vec2 refractOffsetR = normal.xy * (finalStrength * 1.05); // Red bends most
    vec2 refractOffsetG = normal.xy * finalStrength;
    vec2 refractOffsetB = normal.xy * (finalStrength * 0.95); // Blue bends least

    vec2 coordR = clamp(uv - refractOffsetR, 0.0, 1.0);
    vec2 coordG = clamp(uv - refractOffsetG, 0.0, 1.0);
    vec2 coordB = clamp(uv - refractOffsetB, 0.0, 1.0);

    vec2 offsets[8] = vec2[](
        vec2(-halfpixel.x * 2.0, 0.0),
        vec2(-halfpixel.x, halfpixel.y),
        vec2(0.0, halfpixel.y * 2.0),
        vec2(halfpixel.x, halfpixel.y),
        vec2(halfpixel.x * 2.0, 0.0),
        vec2(halfpixel.x, -halfpixel.y),
        vec2(0.0, -halfpixel.y * 2.0),
        vec2(-halfpixel.x, -halfpixel.y)
    );
    float weights[8] = float[](1.0, 2.0, 1.0, 2.0, 1.0, 2.0, 1.0, 2.0);
    float weightSum = 12.0;

    vec4 sum = vec4(0,0,0,0);
    for (int i = 0; i < 8; ++i) {
        vec2 off = offsets[i] * offset;
        sum.r += texture2D(texUnit, coordR + off).r * weights[i];
        sum.g += texture2D(texUnit, coordG + off).g * weights[i];
        sum.b += texture2D(texUnit, coordB + off).b * weights[i];
        sum.a += texture2D(texUnit, coordG + off).a * weights[i];
    }

    sum /= weightSum;

    if (noise) {
        sum += vec4(texture2D(noiseTexture, vec2(uv.x, 1.0 - uv.y) * blurSize / noiseTextureSize).rrr, 0.0);
    }

    gl_FragColor = roundedRectangle(uv * blurSize, sum.rgb);
}