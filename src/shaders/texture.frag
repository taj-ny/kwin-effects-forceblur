uniform sampler2D texUnit;
uniform vec2 textureSize;
uniform vec2 texStartPos;

varying vec2 uv;

void main(void)
{
    vec2 tex = vec2((texStartPos.xy + gl_FragCoord.xy) / textureSize);

    gl_FragColor = vec4(texture2D(texUnit, tex).rgb, 0);
}
