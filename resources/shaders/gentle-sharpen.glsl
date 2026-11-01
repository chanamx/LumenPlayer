//!HOOK SCALED
//!BIND HOOKED
//!DESC Lumen gentle sharpening (optional)
vec4 hook() {
    vec4 c = HOOKED_tex(HOOKED_pos);
    vec3 a = HOOKED_texOff(vec2(-1.0,0.0)).rgb;
    vec3 b = HOOKED_texOff(vec2(1.0,0.0)).rgb;
    vec3 d = HOOKED_texOff(vec2(0.0,-1.0)).rgb;
    vec3 e = HOOKED_texOff(vec2(0.0,1.0)).rgb;
    vec3 lo = min(c.rgb,min(min(a,b),min(d,e)));
    vec3 hi = max(c.rgb,max(max(a,b),max(d,e)));
    return vec4(clamp(c.rgb + 0.12*(c.rgb-(a+b+d+e)*0.25),lo,hi),c.a);
}
