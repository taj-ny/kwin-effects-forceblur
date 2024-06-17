#version 140

uniform sampler2D texUnit;
uniform vec2 textureSize;
uniform vec2 texStartPos;
uniform vec2 regionSize;

in vec2 uv;

out vec4 fragColor;

void main(void)
{
    vec2 tex = (texStartPos.xy + vec2(uv.x, 1.0 - uv.y) * regionSize) / textureSize;
    fragColor = vec4(texture(texUnit, tex).rgb, 0);
}
