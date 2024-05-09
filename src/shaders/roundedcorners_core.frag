#version 140

// Modified version of https://www.shadertoy.com/view/ldfSDj

uniform bool roundTopLeftCorner;
uniform bool roundTopRightCorner;
uniform bool roundBottomLeftCorner;
uniform bool roundBottomRightCorner;

uniform float topCornerRadius;
uniform float bottomCornerRadius;

uniform float antialiasing;

uniform vec2 regionSize;
uniform vec2 offset;

uniform sampler2D beforeBlurTexture;
uniform sampler2D afterBlurTexture;

in vec2 uv;

out vec4 fragColor;

float udRoundBox(vec2 p, vec2 b, vec2 fragCoord)
{
    float radius = 0.0;
    if ((fragCoord.y <= topCornerRadius)
        && ((roundTopLeftCorner && fragCoord.x <= topCornerRadius)
            || (roundTopRightCorner && fragCoord.x >= regionSize.x - topCornerRadius))) {
        radius = topCornerRadius;
    } else if ((fragCoord.y >= regionSize.y - bottomCornerRadius)
        && ((roundBottomLeftCorner && fragCoord.x <= bottomCornerRadius)
            || (roundBottomRightCorner && fragCoord.x >= regionSize.x - bottomCornerRadius))) {
        radius = bottomCornerRadius;
    }

    return length(max(abs(p) - b + radius, 0.0)) - radius;
}

void main(void)
{
    vec2 halfRegionSize = regionSize * 0.5;
    vec2 textureLocation = (gl_FragCoord.xy - offset) / regionSize.xy;
    float box = udRoundBox(-offset + gl_FragCoord.xy - halfRegionSize, halfRegionSize, gl_FragCoord.xy - offset);
    fragColor = vec4(mix(texture(afterBlurTexture, uv).rgb, texture(beforeBlurTexture, uv).rgb, smoothstep(0.0, antialiasing, box)), 1.0);
}
