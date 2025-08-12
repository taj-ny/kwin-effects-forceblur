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
uniform float offset;
uniform vec2 halfpixel;

uniform bool noise;
uniform sampler2D noiseTexture;
uniform vec2 noiseTextureSize;

uniform float edgeSizePixels;
uniform float refractionCornerRadiusPixels;
uniform float refractionStrength;
uniform float refractionNormalPow;
uniform float refractionRGBFringing;
uniform int refractionTextureRepeatMode;
uniform int refractionMode; // 0: Basic, 1: Concave

varying vec2 uv;

vec2 applyTextureRepeatMode(vec2 coord)
{
    if (refractionTextureRepeatMode == 0) {
        return clamp(coord, 0.0, 1.0);
    } else if (refractionTextureRepeatMode == 1) {
        // flip on both axes
        vec2 flip = mod(coord, 2.0);

        vec2 result = coord;
        if (flip.x > 1.0) {
            result.x = 1.0 - mod(coord.x, 1.0);
        } else {
            result.x = mod(coord.x, 1.0);
        }

        if (flip.y > 1.0) {
            result.y = 1.0 - mod(coord.y, 1.0);
        } else {
            result.y = mod(coord.y, 1.0);
        }

        return result;
    }
    return coord;
}

// Concave lens-style radial mapping around the rect center, shaped by distance to edge
vec2 concaveLensCoord(vec2 uv, float strength, float fringing, float dist, vec2 halfBlurSize)
{
    // Edge proximity: 0 in the deep interior, 1 near the rounded rectangle edge
    float edgeProximity = clamp(1.0 + dist / edgeSizePixels, 0.0, 1.0);
    float shaped = sin(pow(edgeProximity, refractionNormalPow) * 1.57079632679);

    vec2 fromCenter = uv - vec2(0.5);

    float scaleR = 1.0 - shaped * strength * (1.0 + fringing);
    float scaleG = 1.0 - shaped * strength;
    float scaleB = 1.0 - shaped * strength * (1.0 - fringing);

    // Return per-channel lens coords packed in vec2 three times via caller
    // Caller samples each channel separately with the right scale
    // Here we just return the green channel scale as a convenience; R and B will be built in caller
    return vec2(0.5) + fromCenter * scaleG;
}

// source: https://iquilezles.org/articles/distfunctions2d/
// https://www.shadertoy.com/view/4llXD7
float roundedRectangleDist(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main(void)
{
    vec2 offsets[8] = vec2[](
        vec2(-halfpixel.x * 2.0, 0.0),
        vec2(-halfpixel.x, halfpixel.y),
        vec2(0.0, halfpixel.y * 2.0),
        vec2(halfpixel.x, halfpixel.y),
        vec2(halfpixel.x * 2.0, 0.0),
        vec2(halfpixel.x, -halfpixel.y),
        vec2(0.0, -halfpixel.y * 2.0),
        vec2(-halfpixel.x, -halfpixel.y)
    );
    float weights[8] = float[](1.0, 2.0, 1.0, 2.0, 1.0, 2.0, 1.0, 2.0);
    float weightSum = 12.0;
    vec4 sum = vec4(0, 0, 0, 0);

    if (refractionStrength > 0) {
        vec2 halfBlurSize = 0.5 * blurSize;
        vec2 position = uv * blurSize - halfBlurSize.xy;
        float cornerR = min(refractionCornerRadiusPixels, min(halfBlurSize.x, halfBlurSize.y));
        float distConcave = roundedRectangleDist(position, halfBlurSize, cornerR);
        float distBulge = roundedRectangleDist(position, halfBlurSize, edgeSizePixels);

        // Different refraction behavior depending on mode
        if (refractionMode == 1) {
            // Concave: lens-like radial mapping with RGB fringing
            float fringing = refractionRGBFringing * 0.3;
            float baseStrength = 0.2 * refractionStrength;

            // Edge proximity shaping
            float edgeProximity = clamp(1.0 + distConcave / edgeSizePixels, 0.0, 1.0);
            float shaped = sin(pow(edgeProximity, refractionNormalPow) * 1.57079632679);

            vec2 fromCenter = uv - vec2(0.5);
            float scaleR = 1.0 - shaped * baseStrength * (1.0 + fringing);
            float scaleG = 1.0 - shaped * baseStrength;
            float scaleB = 1.0 - shaped * baseStrength * (1.0 - fringing);

            vec2 coordR = applyTextureRepeatMode(vec2(0.5) + fromCenter * scaleR);
            vec2 coordG = applyTextureRepeatMode(vec2(0.5) + fromCenter * scaleG);
            vec2 coordB = applyTextureRepeatMode(vec2(0.5) + fromCenter * scaleB);

            for (int i = 0; i < 8; ++i) {
                vec2 off = offsets[i] * offset;
                sum.r += texture2D(texUnit, coordR + off).r * weights[i];
                sum.g += texture2D(texUnit, coordG + off).g * weights[i];
                sum.b += texture2D(texUnit, coordB + off).b * weights[i];
                sum.a += texture2D(texUnit, coordG + off).a * weights[i];
            }

            sum /= weightSum;
        } else {
            // Basic: convex/bulge-like along inward normal from the rounded-rect edge
            float concaveFactor = pow(clamp(1.0 + distBulge / edgeSizePixels, 0.0, 1.0), refractionNormalPow);

            // Initial 2D normal
            const float h = 1.0;
            vec2 gradient = vec2(
                roundedRectangleDist(position + vec2(h, 0), halfBlurSize, edgeSizePixels) - roundedRectangleDist(position - vec2(h, 0), halfBlurSize, edgeSizePixels),
                roundedRectangleDist(position + vec2(0, h), halfBlurSize, edgeSizePixels) - roundedRectangleDist(position - vec2(0, h), halfBlurSize, edgeSizePixels)
            );

            vec2 normal = length(gradient) > 1e-6 ? -normalize(gradient) : vec2(0.0, 1.0);

            float finalStrength = 0.2 * concaveFactor * refractionStrength;

            // Different refraction offsets for each color channel
            float fringingFactor = refractionRGBFringing * 0.3;
            vec2 refractOffsetR = normal.xy * (finalStrength * (1.0 + fringingFactor)); // Red bends most
            vec2 refractOffsetG = normal.xy * finalStrength;
            vec2 refractOffsetB = normal.xy * (finalStrength * (1.0 - fringingFactor)); // Blue bends least

            vec2 coordR = applyTextureRepeatMode(uv - refractOffsetR);
            vec2 coordG = applyTextureRepeatMode(uv - refractOffsetG);
            vec2 coordB = applyTextureRepeatMode(uv - refractOffsetB);

            for (int i = 0; i < 8; ++i) {
                vec2 off = offsets[i] * offset;
                sum.r += texture2D(texUnit, coordR + off).r * weights[i];
                sum.g += texture2D(texUnit, coordG + off).g * weights[i];
                sum.b += texture2D(texUnit, coordB + off).b * weights[i];
                sum.a += texture2D(texUnit, coordG + off).a * weights[i];
            }

            sum /= weightSum;
        }
    } else {
        for (int i = 0; i < 8; ++i) {
            vec2 off = offsets[i] * offset;
            sum += texture2D(texUnit, uv + off) * weights[i];
        }

        sum /= weightSum;
    }

    if (noise) {
        sum += vec4(texture2D(noiseTexture, vec2(uv.x, 1.0 - uv.y) * blurSize / noiseTextureSize).rrr, 0.0);
    }

    gl_FragColor = roundedRectangle(uv * blurSize, sum.rgb);
}