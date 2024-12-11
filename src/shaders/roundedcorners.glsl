// Based on KDE-Rounded-Corners' shader: https://github.com/matinlotfali/KDE-Rounded-Corners/blob/master/src/shaders/shapecorners.glsl

uniform float topCornerRadius;
uniform float bottomCornerRadius;
uniform float antialiasing;

uniform vec2 blurSize;
uniform float opacity;

vec4 shapeCorner(vec2 coord0, vec3 tex, vec2 start, float angle, float r) {
    float diagonal_length = (abs(cos(angle)) > 0.1 && abs(sin(angle)) > 0.1) ? sqrt(2.0) : 1.0;
    vec2 center = start + r * diagonal_length * vec2(cos(angle), sin(angle));
    float distance_from_center = distance(coord0, center);

    return vec4(tex, pow(clamp(r - distance_from_center + 0.5, 0.0, 1.0), antialiasing) * opacity);
}

vec4 roundedRectangle(vec2 fragCoord, vec3 texture)
{
    if (topCornerRadius == 0 && bottomCornerRadius == 0) {
        return vec4(texture, opacity);
    }

    if (fragCoord.y < bottomCornerRadius) {
        if (fragCoord.x < bottomCornerRadius) {
            return shapeCorner(fragCoord, texture, vec2(0.0, 0.0), radians(45.0), bottomCornerRadius);
        } else if (fragCoord.x > blurSize.x - bottomCornerRadius) {
            return shapeCorner(fragCoord, texture, vec2(blurSize.x, 0.0), radians(135.0), bottomCornerRadius);
        }
    }
    else if (fragCoord.y > blurSize.y - topCornerRadius) {
        if (fragCoord.x < topCornerRadius) {
            return shapeCorner(fragCoord, texture, vec2(0.0, blurSize.y), radians(315.0), topCornerRadius);
        } else if (fragCoord.x > blurSize.x - topCornerRadius) {
            return shapeCorner(fragCoord, texture, vec2(blurSize.x, blurSize.y), radians(225.0), topCornerRadius);
        }
    }

    return vec4(texture, opacity);
}
