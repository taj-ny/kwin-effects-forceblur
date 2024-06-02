#version 140

// Modified version of https://www.shadertoy.com/view/ldfSDj

uniform bool roundTopLeftCorner;
uniform bool roundTopRightCorner;
uniform bool roundBottomLeftCorner;
uniform bool roundBottomRightCorner;

uniform int topCornerRadius;
uniform int bottomCornerRadius;

uniform float antialiasing;

uniform vec2 regionSize;

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
        p.y -= radius;
    } else if ((fragCoord.y >= regionSize.y - bottomCornerRadius)
        && ((roundBottomLeftCorner && fragCoord.x <= bottomCornerRadius)
            || (roundBottomRightCorner && fragCoord.x >= regionSize.x - bottomCornerRadius))) {
        radius = bottomCornerRadius;
        p.y += radius;
    }

    return length(max(abs(p) - (b + vec2(0.0, radius)) + radius, 0.0)) - radius;
}

void main(void)
{
    vec2 halfRegionSize = regionSize * 0.5;
    vec2 fragCoord = uv * regionSize;
    float box = udRoundBox(fragCoord - halfRegionSize, halfRegionSize, fragCoord);

    // If antialiasing is 0, the shader will be used to generate corner masks.
    vec3 foreground = vec3(1.0, 1.0, 1.0);
    vec3 background = vec3(0.0, 0.0, 0.0);
    if (antialiasing > 0.0) {
        foreground = texture(afterBlurTexture, uv).rgb;
        background = texture(beforeBlurTexture, uv).rgb;
    }

    fragColor = vec4(mix(foreground, background, smoothstep(0.0, antialiasing, box)), 1.0);
}
