#version 140

#include "roundedcorners.glsl"

uniform sampler2D texUnit;
uniform float offset;
uniform vec2 halfpixel;

uniform bool noise;
uniform sampler2D noiseTexture;
uniform vec2 noiseTextureSize;

in vec2 uv;

out vec4 fragColor;

void main(void)
{
    vec4 sum = texture(texUnit, uv + vec2(-halfpixel.x * 2.0, 0.0) * offset);
    sum += texture(texUnit, uv + vec2(-halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture(texUnit, uv + vec2(0.0, halfpixel.y * 2.0) * offset);
    sum += texture(texUnit, uv + vec2(halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture(texUnit, uv + vec2(halfpixel.x * 2.0, 0.0) * offset);
    sum += texture(texUnit, uv + vec2(halfpixel.x, -halfpixel.y) * offset) * 2.0;
    sum += texture(texUnit, uv + vec2(0.0, -halfpixel.y * 2.0) * offset);
    sum += texture(texUnit, uv + vec2(-halfpixel.x, -halfpixel.y) * offset) * 2.0;
    sum /= 12.0;

    if (noise) {
        sum += vec4(texture(noiseTexture, vec2(uv.x, 1.0 - uv.y) * blurSize / noiseTextureSize).rrr, 0.0);
    }

    fragColor = roundedRectangle(uv * blurSize, sum.rgb);
}