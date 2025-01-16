#version 140

#include "roundedcorners.glsl"

uniform sampler2D texUnit;
uniform vec2 textureSize;
uniform vec2 texStartPos;

in vec2 uv;

out vec4 fragColor;

void main(void)
{
    vec2 tex = (texStartPos.xy + vec2(uv.x, 1.0 - uv.y) * blurSize) / textureSize;
    fragColor = roundedRectangle(uv * blurSize, texture(texUnit, tex).rgb);
}
