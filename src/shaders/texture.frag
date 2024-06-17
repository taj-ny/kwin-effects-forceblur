uniform sampler2D texUnit;
uniform vec2 textureSize;
uniform vec2 texStartPos;
uniform vec2 regionSize;

varying vec2 uv;

void main(void)
{
    vec2 tex = (texStartPos.xy + vec2(uv.x, 1.0 - uv.y) * regionSize) / textureSize;
    gl_FragColor = vec4(texture2D(texUnit, tex).rgb, 0);
}
