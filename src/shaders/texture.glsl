#include "roundedcorners.glsl"

uniform sampler2D texUnit;
uniform vec2 textureSize;
uniform vec2 texStartPos;

varying vec2 uv;

void main(void)
{
    vec2 tex = (texStartPos.xy + vec2(uv.x, 1.0 - uv.y) * blurSize) / textureSize;
    gl_FragColor = roundedRectangle(uv * blurSize, texture2D(texUnit, tex).rgb);
}