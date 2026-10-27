// Temporal resolve. Takes the (usually half-resolution, jittered) cloud pass
// and blends it into a full-resolution history buffer, reprojecting each
// pixel through the previous frame's view-projection using the cloud depth.
in vec2 vUv;
layout(location = 0) out vec4 outColor;

uniform sampler2D uCurrent;     // this frame's cloud pass
uniform sampler2D uCurrentDepth;
uniform sampler2D uHistory;     // last frame's resolved output
uniform mat4 uInvViewProj;      // unjittered, this frame
uniform mat4 uPrevViewProj;     // unjittered, last frame
uniform vec3 uCamPos;
uniform int uHistoryValid;
uniform float uBlend;           // weight of the new sample when history is usable

void main() {
    vec4 cur = texture(uCurrent, vUv);
    if (uHistoryValid == 0) { outColor = cur; return; }

    // World position of what this pixel sees, from the cloud pass depth.
    vec2 ndc = vUv * 2.0 - 1.0;
    vec4 pNear = uInvViewProj * vec4(ndc, -1.0, 1.0);
    vec4 pFar = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 rd = normalize(pFar.xyz / pFar.w - pNear.xyz / pNear.w);
    float depth = texture(uCurrentDepth, vUv).r;
    // Pixels with no cloud reproject at a far distance so parallax is ~zero.
    vec3 worldPos = uCamPos + rd * (depth > 0.0 ? depth : 200000.0);

    vec4 prevClip = uPrevViewProj * vec4(worldPos, 1.0);
    vec2 prevUv = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
    bool onScreen = prevClip.w > 0.0 && all(greaterThanEqual(prevUv, vec2(0.0))) && all(lessThanEqual(prevUv, vec2(1.0)));
    if (!onScreen) { outColor = cur; return; }

    // Clamp the history sample to the neighbourhood of the current sample so
    // stale data (disocclusion, lighting changes) can't ghost.
    vec2 texel = 1.0 / vec2(textureSize(uCurrent, 0));
    vec4 lo = cur, hi = cur;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
        vec4 s = texture(uCurrent, vUv + vec2(x, y) * texel);
        lo = min(lo, s); hi = max(hi, s);
    }
    // Loosen the box slightly: the current pass is noisy by design.
    vec4 pad = (hi - lo) * 0.25 + 0.002;
    lo -= pad; hi += pad;

    vec4 hist = clamp(texture(uHistory, prevUv), lo, hi);
    outColor = mix(hist, cur, uBlend);
}
