// Modified version of https://www.shadertoy.com/view/ldfSDj

uniform bool roundTopLeftCorner;
uniform bool roundTopRightCorner;
uniform bool roundBottomLeftCorner;
uniform bool roundBottomRightCorner;

uniform float topCornerRadius;
uniform float bottomCornerRadius;

uniform float antialiasing;

uniform vec2 regionSize;

uniform sampler2D beforeBlurTexture;
uniform sampler2D afterBlurTexture;

varying vec2 uv;

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
    vec2 fragCoord = uv * regionSize;
    float box = udRoundBox(fragCoord - halfRegionSize, halfRegionSize, fragCoord);
    gl_FragColor = vec4(mix(texture2D(afterBlurTexture, uv).rgb, texture2D(beforeBlurTexture, uv).rgb, smoothstep(0.0, antialiasing, box)), 1.0);
}