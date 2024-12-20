#version 140

uniform sampler2D texUnit;
uniform float offset;
uniform vec2 halfpixel;

uniform bool transformColors;
uniform mat4 colorMatrix;

in vec2 uv;

out vec4 fragColor;

void main(void)
{
    vec4 sum = texture(texUnit, uv) * 4.0;
    sum += texture(texUnit, uv - halfpixel.xy * offset);
    sum += texture(texUnit, uv + halfpixel.xy * offset);
    sum += texture(texUnit, uv + vec2(halfpixel.x, -halfpixel.y) * offset);
    sum += texture(texUnit, uv - vec2(halfpixel.x, -halfpixel.y) * offset);
    sum /= 8.0;

    if (transformColors) {
        sum *= colorMatrix;
    }

    fragColor = sum;
}
