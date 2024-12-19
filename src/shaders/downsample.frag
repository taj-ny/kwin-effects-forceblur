uniform sampler2D texUnit;
uniform float offset;
uniform vec2 halfpixel;

uniform bool transformColors;
uniform mat4 colorMatrix;

varying vec2 uv;

void main(void)
{
    vec4 sum = texture2D(texUnit, uv) * 4.0;
    sum += texture2D(texUnit, uv - halfpixel.xy * offset);
    sum += texture2D(texUnit, uv + halfpixel.xy * offset);
    sum += texture2D(texUnit, uv + vec2(halfpixel.x, -halfpixel.y) * offset);
    sum += texture2D(texUnit, uv - vec2(halfpixel.x, -halfpixel.y) * offset);
    sum /= 8.0;

    if (transformColors) {
        sum *= colorMatrix;
    }

    gl_FragColor = sum;
}
