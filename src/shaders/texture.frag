uniform float topCornerRadius;
uniform float bottomCornerRadius;
uniform float antialiasing;

uniform vec2 blurSize;
uniform float opacity;

vec4 roundedRectangle(vec2 fragCoord, vec3 texture)
{
    if (topCornerRadius == 0 && bottomCornerRadius == 0) {
        return vec4(texture, opacity);
    }

    vec2 halfblurSize = blurSize * 0.5;
    vec2 p = fragCoord - halfblurSize;
    float radius = 0.0;
    if ((fragCoord.y <= bottomCornerRadius)
        && (fragCoord.x <= bottomCornerRadius || fragCoord.x >= blurSize.x - bottomCornerRadius)) {
        radius = bottomCornerRadius;
        p.y -= radius;
    } else if ((fragCoord.y >= blurSize.y - topCornerRadius)
        && (fragCoord.x <= topCornerRadius || fragCoord.x >= blurSize.x - topCornerRadius)) {
        radius = topCornerRadius;
        p.y += radius;
    }
    float distance = length(max(abs(p) - (halfblurSize + vec2(0.0, radius)) + radius, 0.0)) - radius;

    float s = smoothstep(0.0, antialiasing, distance);
    return vec4(texture, mix(1.0, 0.0, s) * opacity);
}


uniform sampler2D texUnit;
uniform vec2 textureSize;
uniform vec2 texStartPos;

varying vec2 uv;

void main(void)
{
    vec2 tex = (texStartPos.xy + vec2(uv.x, 1.0 - uv.y) * blurSize) / textureSize;
    gl_FragColor = roundedRectangle(uv * blurSize, texture2D(texUnit, tex).rgb);
}