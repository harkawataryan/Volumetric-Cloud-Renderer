// Ray-marches the cloud layer. Writes scattered radiance + transmittance
// to one target and a transmittance-weighted mean depth to another, which
// the temporal pass uses to reproject against the previous frame.
in vec2 vUv;
layout(location = 0) out vec4 outColor;   // rgb: in-scattered radiance, a: transmittance
layout(location = 1) out float outDepth;  // metres along the ray, 0 if no cloud

uniform sampler3D uShape;
uniform sampler3D uDetail;
uniform sampler2D uWeather;

uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform float uTime;
uniform uint uFrame;
uniform int uMaxSteps;

uniform float uCoverage;
uniform float uDensity;
uniform float uCloudType;
uniform float uBaseHeight;
uniform float uThickness;
uniform vec2 uWind;
uniform float uAnisotropy;
uniform float uSunIntensity;
uniform float uExtinction;
uniform float uAmbient;
uniform float uErosion;

const float EARTH_RADIUS = 6371000.0;
const float SHAPE_SCALE = 1.0 / 9000.0;     // metres per noise tile
const float DETAIL_SCALE = 1.0 / 900.0;
const float WEATHER_SCALE = 1.0 / 48000.0;
const vec3 AMBIENT_TOP = vec3(0.62, 0.74, 0.95);
const vec3 AMBIENT_BOTTOM = vec3(0.36, 0.42, 0.52);

// Entry/exit distances of a ray through the spherical shell [rInner, rOuter],
// for a camera below, inside or above the layer.
bool shellIntersect(vec3 ro, vec3 rd, float rInner, float rOuter, out float t0, out float t1) {
    vec3 oc = ro - vec3(0.0, -EARTH_RADIUS, 0.0);
    float rCam = length(oc);
    float b = dot(oc, rd);
    float c = dot(oc, oc);
    float hOuter = b * b - (c - rOuter * rOuter);
    if (hOuter < 0.0) return false;
    float sOuter = sqrt(hOuter);
    float tOuterNear = -b - sOuter, tOuterFar = -b + sOuter;
    float hInner = b * b - (c - rInner * rInner);
    float sInner = hInner > 0.0 ? sqrt(hInner) : 0.0;
    float tInnerNear = -b - sInner, tInnerFar = -b + sInner;
    bool hitsInnerAhead = hInner > 0.0 && tInnerNear > 0.0;

    if (rCam < rInner) {               // below: start where we leave the inner sphere
        t0 = tInnerFar; t1 = tOuterFar;
    } else if (rCam > rOuter) {        // above: start at the outer sphere, stop at the inner one
        if (tOuterNear < 0.0) return false;
        t0 = tOuterNear; t1 = hitsInnerAhead ? tInnerNear : tOuterFar;
    } else {                           // inside the layer
        t0 = 0.0; t1 = hitsInnerAhead ? tInnerNear : tOuterFar;
    }
    t0 = max(t0, 0.0);
    return t1 > t0;
}

float heightFraction(vec3 p) {
    float r = length(p - vec3(0.0, -EARTH_RADIUS, 0.0)) - EARTH_RADIUS;
    return clamp((r - uBaseHeight) / uThickness, 0.0, 1.0);
}

// Vertical density profile per cloud type: stratus are flat and low,
// cumulus are taller with a rounded top, cumulonimbus fill the layer.
float heightGradient(float h, float type) {
    float stratus = remap(h, 0.0, 0.08, 0.0, 1.0) * remap(h, 0.15, 0.3, 1.0, 0.0);
    float cumulus = remap(h, 0.0, 0.15, 0.0, 1.0) * remap(h, 0.45, 0.8, 1.0, 0.0);
    float cumulonimbus = remap(h, 0.0, 0.1, 0.0, 1.0) * remap(h, 0.75, 1.0, 1.0, 0.0);
    float a = clamp(type * 2.0, 0.0, 1.0);
    float b = clamp(type * 2.0 - 1.0, 0.0, 1.0);
    return mix(mix(stratus, cumulus, a), cumulonimbus, b);
}

// cheap=true skips the detail erosion; used while searching for the cloud surface.
float sampleDensity(vec3 p, bool cheap) {
    float h = heightFraction(p);
    vec3 wind = vec3(uWind.x, 0.0, uWind.y) * uTime;
    vec3 sp = p + wind + vec3(0.0, 0.0, 0.0);
    sp.xz += h * 300.0;   // slight shear with height

    vec4 weather = texture(uWeather, (p.xz + wind.xz * 0.3) * WEATHER_SCALE);
    float coverage = clamp(weather.r + (uCoverage - 0.5) * 1.6, 0.0, 1.0);
    float type = clamp(weather.g + (uCloudType - 0.5), 0.0, 1.0);
    if (coverage <= 0.001) return 0.0;

    vec4 shape = texture(uShape, sp * SHAPE_SCALE);
    float lowFreq = shape.g * 0.625 + shape.b * 0.25 + shape.a * 0.125;
    float base = remap(shape.r, lowFreq - 1.0, 1.0, 0.0, 1.0);
    base *= heightGradient(h, type);
    base = remap(base, 1.0 - coverage, 1.0, 0.0, 1.0) * coverage;
    if (cheap || base <= 0.0) return max(base, 0.0) * uDensity;

    vec3 detail = texture(uDetail, (sp + vec3(0.0, uTime * 3.0, 0.0)) * DETAIL_SCALE).rgb;
    float detailFbm = detail.r * 0.625 + detail.g * 0.25 + detail.b * 0.125;
    // Wispy at the bottom, billowy at the top.
    float detailMod = mix(detailFbm, 1.0 - detailFbm, clamp(h * 4.0, 0.0, 1.0));
    // Less erosion where coverage is high so solid decks stay solid.
    base = remap(base, detailMod * uErosion * exp(-coverage * 0.75), 1.0, 0.0, 1.0);
    return max(base, 0.0) * uDensity;
}

// Optical depth toward the sun, six samples with growing spacing.
float lightOpticalDepth(vec3 p, vec3 sunDir, float jitter) {
    float od = 0.0;
    float step = uThickness * 0.06;
    float t = step * (0.5 + jitter * 0.5);
    for (int i = 0; i < 6; ++i) {
        vec3 lp = p + sunDir * t;
        od += sampleDensity(lp, i > 2) * step;
        t += step;
        step *= 1.35;
    }
    return od;
}

// Multiple-scattering approximation: octaves with attenuated extinction,
// contribution and phase eccentricity (Wrenninge 2013, Hillaire 2016).
vec3 scatteredLight(float opticalDepth, float cosTheta, float h) {
    float sigma = uExtinction;
    float sun = 0.0;
    float a = 1.0, b = 1.0, c = 1.0;
    for (int k = 0; k < 4; ++k) {
        float phase = 0.65 * phaseHG(cosTheta, uAnisotropy * c) + 0.35 * phaseHG(cosTheta, -0.3 * c);
        sun += a * exp(-opticalDepth * sigma * b) * phase;
        a *= 0.55; b *= 0.45; c *= 0.6;
    }
    // Powder: darken thin, back-lit regions where little in-scattering happens.
    float powder = 1.0 - exp(-opticalDepth * sigma * 2.0);
    sun *= mix(1.0, powder, clamp(cosTheta * 0.5 + 0.5, 0.0, 1.0) * 0.5);
    vec3 ambient = mix(AMBIENT_BOTTOM, AMBIENT_TOP, h) * uAmbient;
    float ambientTerm = exp(-opticalDepth * sigma * 0.2) * 0.7 + 0.1;
    // Warm the sun and dim the ambient as it approaches the horizon.
    float sunset = 1.0 - smoothstep(0.0, 0.35, uSunDir.y);
    vec3 sunColor = mix(vec3(1.0, 0.96, 0.9), vec3(1.0, 0.62, 0.38), sunset);
    ambient *= mix(1.0, 0.6, sunset);
    return sunColor * uSunIntensity * sun + ambient * ambientTerm * (1.0 / PI);
}

void main() {
    vec2 ndc = vUv * 2.0 - 1.0;
    vec4 pNear = uInvViewProj * vec4(ndc, -1.0, 1.0);
    vec4 pFar = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 ro = uCamPos;
    vec3 rd = normalize(pFar.xyz / pFar.w - pNear.xyz / pNear.w);

    float t0, t1;
    float rInner = EARTH_RADIUS + uBaseHeight;
    float rOuter = rInner + uThickness;
    outColor = vec4(0.0, 0.0, 0.0, 1.0);
    outDepth = 0.0;
    if (!shellIntersect(ro, rd, rInner, rOuter, t0, t1)) return;
    // Nothing to see through the ground, and cap the march near the horizon.
    t1 = min(t1, t0 + 60000.0);

    float jitter = ign(gl_FragCoord.xy, uFrame);
    float cosTheta = dot(rd, uSunDir);
    float stepSize = max((t1 - t0) / float(uMaxSteps), 24.0);
    float sigma = uExtinction;

    float t = t0 + jitter * stepSize;
    float trans = 1.0;
    vec3 radiance = vec3(0.0);
    float depthAcc = 0.0, depthWeight = 0.0;

    for (int i = 0; i < uMaxSteps && t < t1; ++i, t += stepSize) {
        vec3 p = ro + rd * t;
        float dens = sampleDensity(p, false);
        if (dens <= 0.0) continue;
        float h = heightFraction(p);
        float od = lightOpticalDepth(p, uSunDir, jitter);
        vec3 light = scatteredLight(od, cosTheta, h);
        float stepTrans = exp(-dens * sigma * stepSize);
        // Energy-conserving integration of in-scattering over the step (Hillaire).
        vec3 integ = (light - light * stepTrans) / max(dens * sigma, 1e-6);
        radiance += trans * integ * dens * sigma;
        float w = trans * (1.0 - stepTrans);
        depthAcc += t * w;
        depthWeight += w;
        trans *= stepTrans;
        if (trans < 0.005) { trans = 0.0; break; }
    }

    // Fade distant clouds into the sky so the horizon doesn't end abruptly.
    float depth = depthWeight > 0.0 ? depthAcc / depthWeight : 0.0;
    float fade = exp(-max(depth - 20000.0, 0.0) / 45000.0);
    radiance *= fade;
    trans = mix(1.0, trans, fade);
    outColor = vec4(radiance, trans);
    outDepth = depth;
}
