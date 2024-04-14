#version 140

uniform sampler2D texUnit;
uniform vec2 textureSize;
uniform vec2 texStartPos;

in vec2 uv;

out vec4 fragColor;

void main(void)
{
    vec2 tex = vec2((texStartPos.xy + gl_FragCoord.xy) / textureSize);

    fragColor = vec4(texture(texUnit, tex).rgb, 0);
}
