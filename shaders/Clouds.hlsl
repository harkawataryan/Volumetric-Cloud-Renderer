cbuffer CameraCB : register(b0)
{
    float4x4 _View;
    float4x4 _Proj;
    float4x4 _PrevViewProj;
    float3   _CamPos; float _Time;
    float2   _Jitter; float _TAA; float _pad0;
};

cbuffer WeatherCB : register(b1)
{
    float3 _SunDir; float _G;
    float  _Coverage; float _Density; float _BaseH; float _Thickness;
    float2 _Wind; float _StepMul; float _JitterEnabled;
};

Texture2D HistoryTex : register(t0);
SamplerState LinearClamp : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSFull(uint id : SV_VertexID) {
    float2 verts[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) };
    float2 p = verts[id];
    VSOut o;
    o.pos = float4(p, 0, 1);
    o.uv = p*0.5+0.5;
    return o;
}

float Hash(float2 p){ return frac(sin(dot(p, float2(127.1,311.7)))*43758.5453); }
float2 Hash2(float2 p){ return frac(sin(float2(dot(p,float2(127.1,311.7)), dot(p,float2(269.5,183.3))))*43758.5453); }

float Noise2(float2 p){
    float2 i=floor(p), f=frac(p);
    float a=Hash(i), b=Hash(i+float2(1,0)), c=Hash(i+float2(0,1)), d=Hash(i+float2(1,1));
    float2 u=f*f*(3-2*f);
    return lerp(lerp(a,b,u.x), lerp(c,d,u.x), u.y);
}

float Worley2(float2 p){
    float2 i=floor(p);
    float minD=1.0;
    [unroll] for(int y=-1;y<=1;y++)
    [unroll] for(int x=-1;x<=1;x++){
        float2 g=float2(x,y);
        float2 o=Hash2(i+g);
        float2 d=g+o-frac(p);
        minD=min(minD, dot(d,d));
    }
    return sqrt(minD);
}

float FBM(float2 p){
    float a=0.0, amp=0.5;
    [unroll] for(int i=0;i<5;i++){
        a += Noise2(p)*amp;
        p*=2.02; amp*=0.5;
    }
    return a;
}
float WorleyFBM(float2 p){
    float a=0.0, amp=1.0;
    [unroll] for(int i=0;i<3;i++){
        a += (1.0 - Worley2(p))*amp;
        p*=2.0; amp*=0.5;
    }
    return saturate(a);
}

float DensityAt(float3 wpos){
    float h = (wpos.y - _BaseH) / max(_Thickness, 1.0);
    if (h<0 || h>1) return 0;
    float2 p = (wpos.xz + _Wind * _Time) * 0.0008;
    float base = FBM(p*1.7);
    float detail = WorleyFBM(p*8.0);
    float d = base*0.7 + detail*0.5;
    d = d - (1.0 - _Coverage);
    d = saturate(d) * _Density * (1.0 - (h-0.5)*(h-0.5)*1.8);
    return d;
}

float PhaseHG(float cosTheta, float g){
    float gg=g*g;
    float denom = pow(1 + gg - 2*g*cosTheta, 1.5);
    return (1 - gg) / max(0.0001, 4*3.14159*denom);
}

struct ColorDepth { float3 col; float trans; };

ColorDepth March(float3 ro, float3 rd, float jitter, float2 uv){
    float t0 = (_BaseH - ro.y)/rd.y;
    float t1 = (_BaseH + _Thickness - ro.y)/rd.y;
    if (t0>t1){ float tt=t0; t0=t1; t1=tt; }
    t0 = max(t0, 0.0);
    if (t1<=0) return (ColorDepth) (float3(0,0,0), 1);

    float dist = t1 - t0;
    float step = 60.0 * _StepMul;
    step *= lerp(1.0, 3.0, saturate(dist/8000.0));

    float t = t0 + jitter * step;
    float3 sun = normalize(_SunDir);
    float trans = 1.0;
    float3 acc = 0;

    [loop] for (int i=0; i<256; ++i){
        if (t>t1 || trans<0.01) break;
        float3 p = ro + rd * t;
        float dens = DensityAt(p);
        if (dens>0.001){
            float lt = 0.0;
            float lstep = step*2.0;
            float ltrans = 1.0;
            [loop] for (int j=0;j<6;j++){
                float3 lp = p + sun * lt;
                float ld = DensityAt(lp);
                ltrans *= exp(-ld * 0.03);
                lt += lstep;
                if (ltrans<0.02) break;
            }
            float mu = dot(rd, sun);
            float phase = PhaseHG(mu, _G);
            float absorb = 0.06;
            float3 scat = float3(1.0, 1.0, 1.0) * phase * ltrans;
            float alpha = 1.0 - exp(-dens * absorb * step);
            acc += trans * scat * alpha;
            trans *= (1.0 - alpha);
        }
        t += step;
        step *= 1.02;
    }
    return (ColorDepth) (acc, trans);
}

float3 SkyColor(float3 rd) {
    float t = saturate(rd.y*0.5+0.5);
    return lerp(float3(0.5,0.6,0.8), float3(0.15,0.35,0.7), t);
}

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 PSClouds(VSOut i) : SV_Target {
    float2 uv = i.uv + _Jitter * (_JitterEnabled>0.5);
    float2 ndc = uv*2-1;
    float4 invPNear = mul(float4(ndc.x, -ndc.y, 0, 1), inverse(_Proj));
    invPNear /= invPNear.w;
    float4 invPFar = mul(float4(ndc.x, -ndc.y, 1, 1), inverse(_Proj));
    invPFar /= invPFar.w;
    float3 rdV = normalize((invPFar - invPNear).xyz);
    float3 ro = _CamPos;
    float3 rd = normalize(mul(float4(rdV,0), inverse(_View)).xyz);

    float jitter = frac(sin(dot(uv, float2(12.9898,78.233))*43758.5453) + _Time)*0.999;
    ColorDepth cd = March(ro, rd, jitter, uv);
    float3 sky = SkyColor(rd);
    float3 color = sky * cd.trans + cd.col;

    float3 hist = HistoryTex.SampleLevel(LinearClamp, i.uv, 0).rgb;
    float alpha = _TAA>0.5 ? 0.10 : 1.0;
    color = lerp(color, hist, 1.0 - alpha);
    return float4(color, 1);
}

float4 PSBlit(VSOut i) : SV_Target {
    return float4(HistoryTex.SampleLevel(LinearClamp, i.uv, 0).rgb, 1);
}
