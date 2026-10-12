// SPDX-License-Identifier: MIT
#pragma once
namespace resolve_dlss5 {
inline constexpr char kTemporalShader[]=R"hlsl(
StructuredBuffer<float4> Current : register(t0);
StructuredBuffer<uint> Guide : register(t1);
StructuredBuffer<float2> Motion : register(t2);
StructuredBuffer<float4> History : register(t3);
StructuredBuffer<uint> PreviousGuide : register(t4);
StructuredBuffer<float4> Low : register(t5);
StructuredBuffer<float4> LowGuide : register(t6);
RWStructuredBuffer<float4> Output : register(u0);
RWStructuredBuffer<float4> NextHistory : register(u1);
RWStructuredBuffer<uint> NextGuide : register(u2);
RWStructuredBuffer<float4> NextLow : register(u3);
RWStructuredBuffer<float4> NextLowGuide : register(u4);
cbuffer Settings : register(b0) {
    uint Width,Height,Mode,UseMotion;
    float Weight,Seconds,Strength;uint Padding;
};
float3 unpack(uint p){return float3(p&255,(p>>8)&255,(p>>16)&255)/255;}
bool validGuide(uint p){return (p>>24)!=0;}
float difference(float3 a,float3 b){float3 e=abs(a-b);return max(e.x,max(e.y,e.z));}
bool position(int2 p,out float2 previous){
    previous=p;bool inside=all(p>=0)&&p.x<int(Width)&&p.y<int(Height);
    if(UseMotion && inside){float2 mv=Motion[(Height-1-p.y)*Width+p.x];previous+=float2(mv.x,-mv.y);}
    return inside && (UseMotion || Mode<2) && all(isfinite(previous))&&all(previous>=0)&&previous.x<=float(Width-1)&&previous.y<=float(Height-1);
}
bool gather(float2 p,float3 color,out float4 result,out float acceptance){
    int2 a=int2(floor(p));float2 f=frac(p);float mass=0,validMass=0;result=0;acceptance=0;bool valid=true;
    [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x){
        float w=(x?f.x:1-f.x)*(y?f.y:1-f.y);if(w<=0)continue;
        int2 n=a+int2(x,y);if(any(n<0)||n.x>=int(Width)||n.y>=int(Height)){valid=false;continue;}
        uint i=n.y*Width+n.x;uint g=PreviousGuide[i];float4 h=History[i];
        if(!validGuide(g)||!all(isfinite(h))||h.a<.999)continue;
        validMass+=w;float q=1-smoothstep(.025,.10,difference(color,unpack(g)));
        mass+=w*q;result+=h*w*q;
    }
    result/=max(mass,1e-20);acceptance=mass;return valid && validMass>=.999 && mass>=.25;
}
bool reconstruct(int2 p,float3 g,out float3 result){
    uint2 size=uint2((Width+1)/2,(Height+1)/2);
    float2 v=float2(p)*.5-.25;int2 a=int2(floor(v));float2 f=frac(v);
    result=0;float mass=0;
    [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x){
        int2 n=clamp(a+int2(x,y),0,int2(size)-1);uint i=n.y*size.x+n.x;
        float4 r=Low[i],guide=LowGuide[i];if(!all(isfinite(r))||!all(isfinite(guide))||r.a<.999||guide.a<.999)continue;
        float w=(x?f.x:1-f.x)*(y?f.y:1-f.y)*(1-smoothstep(.02,.10,difference(g,guide.rgb)));
        mass+=w;result+=r.rgb*w;
    }
    result/=max(mass,1e-20);return mass>=.05;
}
[numthreads(8,8,1)]
void reduce(uint3 id:SV_DispatchThreadID){
    uint2 size=uint2((Width+1)/2,(Height+1)/2);if(any(id.xy>=size))return;
    float3 r=0,g=0;float mass=0;bool valid=true;
    [unroll]for(uint y=0;y<2;++y)[unroll]for(uint x=0;x<2;++x){
        uint2 n=id.xy*2+uint2(x,y);if(n.x>=Width||n.y>=Height)continue;
        uint i=n.y*Width+n.x;uint guide=Guide[i];float4 current=Current[i];
        if(!validGuide(guide)||!all(isfinite(current.rgb))){valid=false;continue;}
        r+=current.rgb;g+=unpack(guide);++mass;
    }
    uint i=id.y*size.x+id.x;float ok=valid&&mass>0?1:0;
    NextLow[i]=float4(mass>0?r/mass:0,ok);NextLowGuide[i]=float4(mass>0?g/mass:0,ok);
}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID){
    if(id.x>=Width||id.y>=Height)return;
    int2 p=id.xy;uint i=id.y*Width+id.x;float4 input=Current[i];uint guide=Guide[i];
    float3 d=input.rgb,g=unpack(guide);NextGuide[i]=guide;
    if(!validGuide(guide)||!all(isfinite(d))||all(d==0)){
        NextHistory[i]=0;Output[i]=float4(all(isfinite(d))?d:0,0);return;
    }
    float3 current=d,detail=0;
    if(Mode==4){
        if(!reconstruct(p,g,current)){NextHistory[i]=float4(d,0);Output[i]=float4(d,0);return;}
        detail=d-current;
    }
    float observed=Mode==3?input.a:1,support=observed;float3 result=current;
    float2 previous=0;float4 old=0;float tap=0;
    bool valid=Weight>0&&position(p,previous)&&gather(previous,g,old,tap);
    float sum=0,maximum=0,mass=0;float3 lo=d,hi=d;
    if(valid){
        [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
            int2 n=p+int2(x,y);if(any(n<0)||n.x>=int(Width)||n.y>=int(Height))continue;
            uint j=n.y*Width+n.x;float2 previousN=0;float4 sample=0;float qa=0;
            float3 r=Current[j].rgb,ng=unpack(Guide[j]);
            if(!validGuide(Guide[j])||!all(isfinite(r))||!position(n,previousN)||
                (UseMotion&&any(abs((previousN-n)-(previous-p))>2))||!gather(previousN,ng,sample,qa)){
                valid=false;continue;
            }
            uint2 nearest=uint2(floor(previousN+.5));float e=difference(ng,unpack(PreviousGuide[nearest.y*Width+nearest.x]));
            sum+=e;maximum=max(maximum,e);++mass;lo=min(lo,r);hi=max(hi,r);
        }
    }
    float q=valid&&mass>0?tap*(1-smoothstep(.008,.04,sum/mass))*(1-smoothstep(.025,.10,maximum)):0;
    if(q>0){
        float3 margin=.02*(1+max(abs(lo),abs(hi)))+q*abs(old.rgb);
        float3 safe=clamp(old.rgb,lo-margin,hi+margin);
        if(Mode==3){
            float oldSupport=saturate(old.a-1);
            float memory=exp(-Seconds/(observed>oldSupport?.06:.18))*q;
            support=lerp(observed,oldSupport,memory);
            if(oldSupport<=.001||(observed>0&&dot(current,old.rgb)<0)){result=current;support=observed;}
            else result=lerp(safe,current,(1-Weight*q)*observed);
        }else result=lerp(current,safe,Weight*q);
    }
    NextHistory[i]=float4(result,Mode==3?1+support:1);
    float3 stable=result*(Mode==3?support:1)+detail;
    Output[i]=float4(lerp(d,stable,Strength),q>0?1:0);
}
)hlsl";
}
