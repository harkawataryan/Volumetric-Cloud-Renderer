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
    vec3 zenith = mix(vec3(0.18, 0.36, 0.78), vec3(0.16, 0.24, 0.50), sunset);
    vec3 horizon = mix(vec3(0.62, 0.74, 0.90), vec3(0.95, 0.66, 0.48), sunset);
    vec3 sky = mix(horizon, zenith, pow(up, 0.55));
    float sunAmount = max(dot(rd, uSunDir), 0.0);
    // Mie-ish glow around the sun plus the disc itself.
    sky += mix(vec3(1.0, 0.85, 0.6), vec3(1.0, 0.5, 0.25), sunset) * pow(sunAmount, 8.0) * (0.25 + 0.5 * sunset);
    sky += vec3(1.0, 0.95, 0.85) * smoothstep(0.9993, 0.9997, sunAmount) * 12.0;
    return sky;
}

vec3 groundColor(vec3 ro, vec3 rd) {
    float t = -ro.y / rd.y;
    vec3 p = ro + rd * t;
    float checker = step(0.5, fract(p.x * 0.0005)) == step(0.5, fract(p.z * 0.0005)) ? 1.0 : 0.85;
    vec3 ground = vec3(0.22, 0.27, 0.18) * checker;
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
    vec3 color = background * clouds.a + clouds.rgb;
    color = aces(color * 0.9);
    outColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
