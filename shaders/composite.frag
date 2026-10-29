// Sky, sun, ground and clouds combined, then tone-mapped.
in vec2 vUv;
layout(location = 0) out vec4 outColor;

uniform sampler2D uClouds;      // resolved cloud radiance + transmittance
uniform sampler2D uCloudDepth;
uniform sampler3D uShape;       // only for the debug noise view
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform int uShowNoise;

vec3 skyColor(vec3 rd) {
    float up = clamp(rd.y, 0.0, 1.0);
    float sunset = 1.0 - smoothstep(0.0, 0.35, uSunDir.y);
    vec3 zenith = mix(vec3(0.16, 0.34, 0.78), vec3(0.14, 0.20, 0.46), sunset);
    vec3 horizon = mix(vec3(0.62, 0.74, 0.90), vec3(1.00, 0.58, 0.36), sunset);
    vec3 sky = mix(horizon, zenith, pow(up, 0.5));
    float sunAmount = max(dot(rd, uSunDir), 0.0);
    // Mie-ish glow around the sun in two widths: a wide warm haze and a tight halo.
    vec3 glow = mix(vec3(1.0, 0.85, 0.65), vec3(1.0, 0.45, 0.18), sunset);
    sky += glow * (pow(sunAmount, 6.0) * (0.2 + 0.6 * sunset) + pow(sunAmount, 120.0) * (0.6 + 1.2 * sunset));
    // Small disc with a soft limb.
    sky += vec3(1.0, 0.94, 0.82) * smoothstep(0.99985, 0.99996, sunAmount) * 20.0;
    return sky;
}

vec3 groundColor(vec3 ro, vec3 rd) {
    float t = -ro.y / rd.y;
    vec3 p = ro + rd * t;
    // Low-contrast patchy land instead of a test checkerboard.
    float n = perlin2(p.xz * 0.00012, 64) * 0.6 + perlin2(p.xz * 0.0006, 256) * 0.4;
    vec3 ground = mix(vec3(0.16, 0.20, 0.12), vec3(0.28, 0.30, 0.18), smoothstep(0.35, 0.65, n));
    float sunLit = max(uSunDir.y, 0.0) * 0.8 + 0.2;
    ground *= sunLit;
    float fog = 1.0 - exp(-t / 45000.0);
    return mix(ground, skyColor(vec3(rd.x, 0.0, rd.z)), fog);
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    if (uShowNoise != 0) {
        // Debug: slice through the shape texture.
        vec4 n = texture(uShape, vec3(vUv, fract(uCamPos.x * 0.0001)));
        outColor = vec4(vUv.x < 0.5 ? n.rrr : n.gba, 1.0);
        return;
    }
    vec2 ndc = vUv * 2.0 - 1.0;
    vec4 pNear = uInvViewProj * vec4(ndc, -1.0, 1.0);
    vec4 pFar = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 rd = normalize(pFar.xyz / pFar.w - pNear.xyz / pNear.w);

    vec3 background = rd.y < 0.0 && uCamPos.y > 0.0 ? groundColor(uCamPos, rd) : skyColor(rd);
    vec4 clouds = texture(uClouds, vUv);
    // Aerial perspective: distant clouds lose contrast toward the horizon haze.
    float depth = texture(uCloudDepth, vUv).r;
    float haze = 1.0 - exp(-max(depth - 10000.0, 0.0) / 50000.0);
    vec3 hazeColor = skyColor(normalize(vec3(rd.x, max(rd.y, 0.02), rd.z)));
    vec3 cloudRgb = mix(clouds.rgb, hazeColor * (1.0 - clouds.a), haze * 0.8);
    vec3 color = background * clouds.a + cloudRgb;
    color = aces(color * 1.05);
    outColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
