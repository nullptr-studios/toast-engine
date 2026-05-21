// turboCoolShader.slang

// --------------------------------------------------------------------------
// HARDCODED UNIFORMS & RESOURCES (No Descriptors Required)
// --------------------------------------------------------------------------
static const float  iTime = 15.0; // Hardcoded time for a nice wave/cloud state
static const float3 iResolution = float3(1920.0, 1080.0, 1.0); // Default 1080p
static const float4 iMouse = float4(0.0);

// --------------------------------------------------------------------------
// GLSL -> SLANG/HLSL BRIDGING MACROS & FUNCTIONS
// --------------------------------------------------------------------------
#define vec2 float2
#define vec3 float3
#define vec4 float4
#define mat2 float2x2
#define mat3 float3x3
#define mix lerp
#define fract frac

float glsl_mod(float x, float y) { return x - y * floor(x / y); }
vec2 glsl_mod(vec2 x, vec2 y) { return x - y * floor(x / y); }
vec3 glsl_mod(vec3 x, vec3 y) { return x - y * floor(x / y); }
vec3 glsl_mod(vec3 x, float y) { return x - y * floor(x / y); }

// --------------------------------------------------------------------------
// CONSTANTS
// --------------------------------------------------------------------------
#define PI 3.14159
#define TWO_PI 6.28318
#define PI_OVER_TWO 1.570796

#define REALLY_SMALL_NUMBER 0.0001
#define REALLY_BIG_NUMBER 1000000.

#define BRIDGE_SURFACE_ID 1.
#define ISLAND_SURFACE_ID 2.
#define SKY_SURFACE_ID 3.
#define WATER_SURFACE_ID 4.
#define SUSPENSION_STRUTS_SURFACE_ID 5.
#define BRIDGE_LIGHT_SURFACE_ID 6.
#define MOON_SURFACE_ID 7.

#define MOON_COLOR vec3(.91, .93, 1.)

#define DIST_MARCH_STEPS 50
#define DIST_MARCH_MAXDIST 44.

#define MATCHES_SURFACE_ID(id1, id2) (id1 > (id2 - .5)) && (id1 < (id2 + .5))
#define NUM_AA_SAMPLES 1.

// --------------------------------------------------------------------------
// GLOBALS
// --------------------------------------------------------------------------
static vec4  g_debugcolor  = vec4(0.0);
static float g_time        = 0.0;
static float g_exposure    = 1.0;
static float g_focus       = 0.9;
static vec3  g_moonpos     = vec3(0.0);
static vec3  g_camorigin   = vec3(0.0);
static vec3  g_campointsat = vec3(0.0);

// --------------------------------------------------------------------------
// INTER-STAGE STRUCTURE
// --------------------------------------------------------------------------
struct VS_Output
{
    float4 position : SV_Position;
};

// --------------------------------------------------------------------------
// MATH UTILITIES (Procedural Noise)
// --------------------------------------------------------------------------

// Pseudo-random hash function
float hash(float n) { 
    return frac(sin(n) * 43758.5453123); 
}

// Procedural 3D Value Noise (Replaces the iChannel0 texture lookup)
float noise( in vec3 x )
{
    vec3 p = floor(x);
    vec3 f = frac(x);
    f = f * f * (3.0 - 2.0 * f);
    float n = p.x + p.y * 157.0 + 113.0 * p.z;
    return lerp(
        lerp(lerp(hash(n + 0.0), hash(n + 1.0), f.x),
             lerp(hash(n + 157.0), hash(n + 158.0), f.x), f.y),
        lerp(lerp(hash(n + 113.0), hash(n + 114.0), f.x),
             lerp(hash(n + 270.0), hash(n + 271.0), f.x), f.y), f.z);
}

vec3 fresnel(vec3 R, vec3 N, float eta)
{
    float ro = (1. - eta) / (1. + eta);
    ro *= ro;
    float fterm = pow(max(0., 1. - dot(R, N)), 5.);
    return vec3(ro + ( 1. - ro ) * fterm);
}

vec3 rotate_xaxis( vec3 point_val, float cosa, float sina )
{
    return vec3(point_val.x,
                point_val.y * cosa - point_val.z * sina,
                point_val.y * sina + point_val.z * cosa);
}

vec3 rotate_yaxis( vec3 point_val, float cosa, float sina )
{
    return vec3(point_val.x * cosa  + point_val.z * sina,
                point_val.y,
                point_val.x * -sina + point_val.z * cosa);
}

vec2 intersect_plane(vec3 ro, vec3 rd, vec3 pn, vec3 po)
{
    float rddn = dot(rd, pn);
    float intersected = 0.;
    float t = REALLY_BIG_NUMBER;
    if (abs(rddn) > REALLY_SMALL_NUMBER) {
        t = -dot(pn, (ro - po)) / rddn;
        if (t > REALLY_SMALL_NUMBER) {
            intersected = 1.;
        } else {
            t = REALLY_BIG_NUMBER;
        }
    }
    return vec2(intersected, t);
}

vec3 intersect_sphere(vec3 ro, vec3 rd, float sphr, vec3 sphc)
{
    vec3 oro = ro - sphc;
    float a = dot(rd, rd);
    float b = dot(oro, rd);
    float c = dot(oro, oro) - sphr*sphr;
    float discr = b*b - a*c; 
    
    float tmin = 0.0;
    float tmax = 0.0;

    float sdisc = sqrt(max(0.0, discr));
    tmin = (-b - sdisc)/a;
    tmax = (-b + sdisc)/a; 

    float hit = step(0., tmin);
    float outside = step(sphr*sphr, dot(oro, oro));
    return outside * vec3(hit, tmin, tmax);
}

vec3 intersect_isphere(vec3 ro, vec3 rd, float sphr, vec3 sphc)
{
    vec3 oro = ro - sphc;
    float a = dot(rd, rd);
    float b = dot(oro, rd);
    float c = dot(oro, oro) - sphr*sphr;
    float discr = b*b - a*c; 
    
    float tmin = 0.0;
    float tmax = 0.0;

    float sdisc = sqrt(max(0.0, discr));
    tmin = (-b - sdisc)/a;
    tmax = (-b + sdisc)/a; 

    float hit = step(0., tmax);
    return vec3(hit, tmin, tmax);
}

float flow_noise( in vec3 p )
{
    vec3 q = p - vec3(0., .5 * g_time, 0.);
    float f;
    f  = 0.50000 * noise( q );
    q = q * 3.02 - vec3(0., .5 * g_time, 0.);
    f += 0.35000 * noise( q );
    q = q * 3.03;
    f += 0.15000 * noise( q ); q = q * 3.01;
    return f;
}

float map4( in vec3 p )
{
    vec3 q = p;
    float f;
    f  = 0.50000 * noise( q ); q = q * 2.02;
    f += 0.25000 * noise( q );
    q = q * 2.03;
    f += 0.12500 * noise( q ); q = q * 2.01;
    f += 0.06250 * noise( q );
    return f;
}

void composite(inout vec4 ci, vec4 ca)
{
    ci += ca * (1. - ci.a);
}

vec3 nearest_point_on_line( vec3 a, vec3 b, vec3 p)
{
    vec3 ba = b - a;
    float t = dot(ba, (p - a)) / dot(ba, ba);
    return a + t * ba;
}

float periodicmix(float v1, float v2, float v3, float x)
{
    float modx = glsl_mod(x, 3.);
    return mix(v1, 
                mix(v2, 
                    mix(v3, v1, smoothstep(.5, .6, modx - 2.)), 
                    smoothstep(.5, .6, modx - 1.)), 
                smoothstep(.5, .6, modx));
}

float sphere_df( vec3 p, float r ) { return length( p ) - r; }
float roundbox_df ( vec3 p, vec3 b, float r ) { return length(max(abs(p - vec3(0., .5*b.y, 0.)) - .5*b, 0.0)) - r; }

// --------------------------------------------------------------------------
// DATA STRUCTURES
// --------------------------------------------------------------------------

struct CameraInfo
{
    vec3 camera_origin;
    vec3 ray_look_direction;
    mat3 camera_transform;
    vec2 image_plane_uv;
};

struct SurfaceInfo
{
    float surface_id;
    vec3 view_origin;
    vec3 view_dir;
    vec3 surface_point;
    vec3 surface_normal;
    vec2 surface_uv;
    float surface_depth;
    float shade_in_reflection;
};

struct MaterialInfo
{
    vec3 bump_normal;
    vec3 diffuse_color;
    vec3 specular_color; 
    float specular_exponent; 
    float reflection_intensity;
    vec3 emissive_color;
};

// --------------------------------------------------------------------------
// SETUP WORLD
// --------------------------------------------------------------------------

void setup_globals()
{
    g_time = 1. * iTime;
    g_exposure = 1.;
    vec2 click = iMouse.xy / iResolution.xx;
    click = 2.0 * click - 1.0;  
    
    g_camorigin = vec3(0.0, .1, 7.0);
    float rotxang = .4 * PI * -0.0;
    rotxang += -.02 * cos(.02 * g_time) - .03;
    float cosrotxang = cos(rotxang);
    float sinrotxang = sin(rotxang);
    g_camorigin = rotate_xaxis(g_camorigin, cosrotxang, sinrotxang);
    
    float rotyang = PI * .28;
    rotyang += .05 * sin(.2 * g_time);
    float cosrotyang = cos(rotyang);
    float sinrotyang = sin(rotyang);    
    g_camorigin = rotate_yaxis(g_camorigin, cosrotyang, sinrotyang);
    g_campointsat = vec3(0., .55, 1.5);
    vec3 iu = vec3(0., 1., 0.);
    vec3 iz = normalize( g_campointsat - g_camorigin );
    vec3 ix = normalize( cross(iz, iu) );
    vec3 iy = cross(ix, iz);
    
    float inv_aspect_ratio = iResolution.y / iResolution.x;
    vec2 click_uv = iMouse.xy / iResolution.xy - .5;
    click_uv.y *= inv_aspect_ratio;
    vec3 ray_click_dir = normalize( click_uv.x * ix + click_uv.y * iy + g_focus * iz );
    vec3 result = intersect_sphere(g_camorigin, ray_click_dir, 40., g_camorigin);

    if (iMouse.x + iMouse.y > 1.) {
        g_moonpos = g_camorigin + result.z * ray_click_dir;
    } else {
        g_moonpos = vec3(-28., 8., 3.);
    }
}

CameraInfo setup_camera(vec2 aaoffset, float2 fragCoord)
{
    float inv_aspect_ratio = iResolution.y / iResolution.x;
    vec2 image_plane_uv = (fragCoord.xy + aaoffset) / iResolution.xy - .5;
    image_plane_uv.y *= inv_aspect_ratio;
    
    vec3 iu = vec3(0., 1., 0.);
    vec3 iz = normalize( g_campointsat - g_camorigin );
    vec3 ix = normalize( cross(iz, iu) );
    vec3 iy = cross(ix, iz);
    vec3 ray_look_direction = normalize( image_plane_uv.x * ix + image_plane_uv.y * iy + g_focus * iz );
    
    CameraInfo info;
    info.camera_origin = g_camorigin;
    info.ray_look_direction = ray_look_direction;
    info.camera_transform = mat3(ix, iy, iz); 
    info.image_plane_uv = image_plane_uv;
    
    return info;
}

vec2 mergeobjs(vec2 a, vec2 b) { return mix(b, a, step(a.x, b.x)); }
float uniondf(float a, float b) { return min(a, b); }
float intersdf(float a, float b) { return max(a, b); }
float diffdf(float a, float b) { return max(a, -b); }

vec2 scene_df( vec3 p )
{
    vec3 gp = p * vec3(.2, 1., .2) + vec3(0., 10., 0.) * (.5 * map4(.2 * p) - .5) + vec3(7.2, 3.3, -2.);
    vec2 ground_obj = vec2(sphere_df(gp, 2.9), ISLAND_SURFACE_ID);    

    vec3 mp = p - g_moonpos;
    float mdf = sphere_df(mp, 1.2);
    vec3 bb = p;
    bb.x = glsl_mod(bb.x + 4., 8.) - 4.;
    bb.z = abs(bb.z) - .15;
    float bdf = roundbox_df( bb, vec3(.1, 2.5, .03), .01);
    bdf = uniondf(bdf, roundbox_df( bb, vec3(.35, .12, .15), .01));
    bdf = uniondf(bdf, roundbox_df( bb - vec3(0., 2.45, -0.1), vec3(.11, .1, .3), .01));
    bdf = uniondf(bdf, roundbox_df( bb - vec3(0., .92, -0.1), vec3(8., .02, .2), .01));
    bdf = uniondf(bdf, roundbox_df( bb - vec3(0., 1.05, -0.1), vec3(8., .02, .2), .01));
                 
    vec3 cb = bb;
    cb.y = abs(glsl_mod(cb.y + .19, .38) - .19);
    float m = step(2.5, bb.y) + step(.95, bb.y) * step(bb.y, 1.3);
    cb.y -= .15 + REALLY_BIG_NUMBER * m;
    
    cb.yz = mul(mat2(-.707, -.707, .707, -.707), cb.yz);
    bdf = uniondf(bdf, roundbox_df( cb, vec3(.03, 1., .03), .01)); 

    vec3 sp = p;
    sp.x = glsl_mod(sp.x, 8.) - 4.;
    sp.z = abs(sp.z) - .15;
    sp.y -= .09 * sp.x*sp.x;
    bdf = uniondf(bdf, roundbox_df( sp - vec3(0., 1.1, 0.), vec3(8., .01, .01), .01));
        
    vec3 tp = p;
    tp.x = glsl_mod(tp.x + .05, .1) - .05;
    tp.z -= .13;
    float sdf = roundbox_df(tp - vec3(0., 1.06, 0.), vec3(.0, .089 * sp.x*sp.x + .02, .0), .005);
    float wldf = sphere_df(bb - vec3(0.0, 2.64, -.15), .03);
    
    vec2 bridge_obj = vec2(bdf, BRIDGE_SURFACE_ID); 
    vec2 suspension_obj = vec2(sdf, SUSPENSION_STRUTS_SURFACE_ID);
    vec2 light_obj = vec2(wldf, BRIDGE_LIGHT_SURFACE_ID); 
    vec2 moon_obj = vec2(mdf, MOON_SURFACE_ID);
    
    vec2 obj = ground_obj;
    obj = mergeobjs(obj, bridge_obj);
    obj = mergeobjs(obj, suspension_obj);
    obj = mergeobjs(obj, light_obj);
    obj = mergeobjs(obj, moon_obj);
    return obj;
}

vec3 calc_normal( vec3 p )
{
    vec3 epsilon = vec3( 0.01, 0.0, 0.0 );
    vec3 n = vec3(
        scene_df(p + epsilon.xyy).x - scene_df(p - epsilon.xyy).x,
        scene_df(p + epsilon.yxy).x - scene_df(p - epsilon.yxy).x,
        scene_df(p + epsilon.yyx).x - scene_df(p - epsilon.yyx).x );
    return normalize( n );
}

vec2 intersect_water(vec3 ro, vec3 rd) { return intersect_plane(ro, rd, vec3(0., 1., 0.), vec3(0., 0., 0.)); }

SurfaceInfo march_scene(vec3 ray_origin, vec3 ray_direction, float consider_water)
{
    SurfaceInfo surface;
    surface.surface_id = -1.;
    surface.view_origin = ray_origin;
    surface.view_dir = ray_direction;
    surface.surface_point = vec3(0.);
    surface.surface_normal = vec3(0.);
    surface.surface_uv = vec2(0.);
    surface.surface_depth = 0.;
    surface.shade_in_reflection = 0.;
    vec2 water = consider_water * intersect_water(ray_origin, ray_direction);
    
    float epsilon = 0.001;
    float dist = 10. * epsilon;
    float total_t = 0.;
    float curr_t = 0.;
    
    vec3 ro = ray_origin;
    vec3 rd = ray_direction;
    for (int i=0; i < DIST_MARCH_STEPS; i++) 
    {
        if ( abs(dist) < epsilon || curr_t + total_t > DIST_MARCH_MAXDIST ) break;
        vec3 p = ro + curr_t * rd;
        vec2 dfresult = scene_df( p );
        vec2 sky_obj = vec2(-sphere_df(p, 35.), SKY_SURFACE_ID);
        dfresult = mergeobjs(sky_obj, dfresult);

        dist = dfresult.x;        
        curr_t += dist;
        surface.surface_id = dfresult.y;
        if ( water.x > .5 && curr_t > water.y )
        {
            surface.surface_id = WATER_SURFACE_ID;
            curr_t = water.y;
            break;
        }                                                            
    }
    
    surface.surface_point = ro + curr_t * rd;
    total_t += curr_t;
    
    if (MATCHES_SURFACE_ID(surface.surface_id, WATER_SURFACE_ID))
    {
        vec3 n = vec3(0., 1., 0.);
        vec3 u = normalize(-vec3(1., 0., 1.) * ray_origin);
        vec3 v = cross(n, u);
        surface.surface_uv = vec2(100., 10.) * vec2( dot(surface.surface_point, u), dot(surface.surface_point, v) );
        n += u * (.2 * flow_noise(surface.surface_uv.xxy) - .1);
        surface.surface_normal = normalize(n);
    }    
    else if (MATCHES_SURFACE_ID(surface.surface_id, SKY_SURFACE_ID))
    {
        surface.surface_normal = -rd;
        surface.surface_uv = surface.surface_point.xz;
    }
    else 
    {        
        surface.surface_normal = calc_normal( surface.surface_point );
        surface.surface_uv = surface.surface_point.xz;
    }
            
    surface.surface_depth = total_t;
    return surface;
}

vec3 light_from_point_light(SurfaceInfo surface, MaterialInfo material, vec3 light_position, vec3 light_color, float falloff_with_distance, float specular_sharpen)
{
    vec3 light_direction = normalize(light_position - surface.surface_point);
    vec3 light_reflection_direction = reflect(light_direction, material.bump_normal);
    vec3 reflective_shading = material.specular_color * pow(max(0., dot(light_reflection_direction, surface.view_dir)), material.specular_exponent * specular_sharpen);
    float ldist = length(surface.surface_point - light_position);
    float dist_atten = 1. / ldist;
    vec3 diffuse_shading = material.diffuse_color * max(0., dot(light_direction, material.bump_normal)) * mix(1., dist_atten, falloff_with_distance);    
    return light_color * (diffuse_shading + reflective_shading);
}

vec4 shade_clouds(vec3 ro, vec3 rd, float depth)
{
    vec4 cloud_rgba = vec4(0.);
    float num_clouds = 0.;
    for (float i = 0.; i < 3.; i += 1.)
    {
        vec3 ch = intersect_isphere(ro, rd, 3.1 * i + 4.5, ro);
        if (ch.x > .5 && ch.z < depth )
        {
            vec3 hp = ro + rd * ch.z;
            vec3 hpo = nearest_point_on_line(ro, g_moonpos, hp);
            vec3 uvhp = hp - vec3(-28., 9., 9.);
            uvhp *= 2.;
            float height_s = smoothstep(1., 5., hp.y);
            float cloud_alpha = (.06 + .02 * i) * (.2 + .8 * smoothstep(.1, .9 - .25 * height_s, map4(vec3(1., .8, 0.) * uvhp.yxz + vec3(3. * i, 5. * i , .05 * g_time))));
            cloud_alpha *= smoothstep(0.5, 1.5, hp.y) * smoothstep(2. + 1.8 * i, .0, hp.y);
            vec3 cloud_color = 2. * vec3(.6,.8,1.+.1*i);
            vec3 halod = hp - hpo;
            vec3 halo_color = .8 * MOON_COLOR * max(0., 4. - length(2. * halod));
            cloud_color += .6 * pow(halo_color, vec3(2.));
            composite(cloud_rgba, vec4(cloud_color * cloud_alpha, cloud_alpha));            
            num_clouds += 1.;
        }
        else break;
    }
    return cloud_rgba;
}

vec3 shade_reflected_world(SurfaceInfo surface)
{
    MaterialInfo material;
    material.bump_normal = surface.surface_normal;
    material.diffuse_color = vec3(0.);
    material.specular_color = vec3(0.);
    material.specular_exponent = 1.;
    material.reflection_intensity = 1.;
    material.emissive_color = vec3(0.);

    if (MATCHES_SURFACE_ID(surface.surface_id, BRIDGE_SURFACE_ID))
    {
        material.diffuse_color = .02 * vec3(.65, .62, .68);
        material.specular_color = .5 * vec3(0.5, 0.6, 0.7);
        material.specular_exponent = 30.;
        material.emissive_color = vec3(.04, .03, .035);
    } 
    else if (MATCHES_SURFACE_ID(surface.surface_id, BRIDGE_LIGHT_SURFACE_ID))
    {
        float i = abs(sin(g_time));
        material.emissive_color = i*i * vec3(2., .05, .05);
        material.reflection_intensity = 10.;
    }
    else if (MATCHES_SURFACE_ID(surface.surface_id, SUSPENSION_STRUTS_SURFACE_ID))
    {
        vec3 dp = surface.surface_point.xyz;
        dp.x = glsl_mod(dp.x + 4., 8.) - 4.;
        float s = (.5 * cos(6. * dp.y * pow(abs(dp.x), .5) - 4. * g_time ) + .5);
        float c1 = smoothstep(.3, 1., s);

        s = (.5 * cos(4. * dp.y * dp.x + mix(-1., 1., step(0., dp.x))* 5. * g_time ) + .5);
        float c2 = smoothstep(.2, .8, s);
           
        dp = surface.surface_point.xyz;
        s = .5 * cos(2. * dp.x - 2.* g_time + 5. * 1. * sin(3. * dp.y + 1. * g_time)) + .5;
        float c3 = smoothstep(.2, .9, s);
        
        float c = periodicmix(c1, c2, c3, .2 * g_time + .3 * dp.y);
        float l = glsl_mod(20. * dp.y, 1.);
        c *= smoothstep(0.2, .5, l) * smoothstep(.8, .5, l);
        material.emissive_color = 2. * c * vec3(.9, .9, 1.);
        material.reflection_intensity = 8.;
    } 
    else if (MATCHES_SURFACE_ID(surface.surface_id, ISLAND_SURFACE_ID))
    {
        vec3 surface_color = .05 * vec3(1., .5, .7);
        material.diffuse_color = surface_color;
        material.emissive_color = .1 * surface_color;
        material.specular_color = .1 * surface_color;
    }
    else if (MATCHES_SURFACE_ID(surface.surface_id, SKY_SURFACE_ID))
    {
        vec3 sky_color = .6 * vec3(0.15, 0.12, .12) * smoothstep(3., 0., surface.surface_point.y);
        float l = smoothstep(2., 0., surface.surface_point.y);
        sky_color += .5 * vec3(0.2, 0.14, .12) * l * l;
        vec3 cn = surface.surface_point;
        cn.z = floor(20. * cn.z) * .05;
        float city_noise = noise(8. * cn.xzz);
        sky_color += 2. * vec3(1., .8, .7) * smoothstep(.25 * city_noise - .05, -.06, surface.surface_point.y);
        material.emissive_color = sky_color;
    }
    else if (MATCHES_SURFACE_ID(surface.surface_id, MOON_SURFACE_ID))
    {
        mat3 om = mat3( 0.00,  0.80,  0.60,
                       -0.80,  0.36, -0.48,
                       -0.60, -0.48,  0.64 );
        vec3 mp = 1.3 * mul(om, (surface.surface_point - g_moonpos));

        float moon = .1;
        moon += .6 * smoothstep( 0.2,  .8, noise( .6 * (mp - vec3(1., 0., 0.)) ));
        mp = 1.2 * mul(om, mp); 
        moon += .5 * smoothstep( 0.2,  1., noise( .8 * (mp - vec3(1., 0., 0.))));
        mp = 2.8 * mul(om, mp); 
        moon += 0.300 * smoothstep( 0.1,  1.3, pow(noise( mp ), 3.0));
        moon *= .8 + .2 * smoothstep( 0.5,  0.7, noise( 1.7 * mp ));
        moon *= .6 + .4 * noise( 30.5 * mp); 
        moon += 0.35 * pow(noise( 15.5 * mp), 6.);
        material.emissive_color = moon * .6 * MOON_COLOR;
        material.reflection_intensity = 4.;
    }
      
    vec4 scene_rgba = vec4(0.05, .05, .08, 0.1);
    vec3 em = material.emissive_color * mix(1., material.reflection_intensity, step(0.5, surface.shade_in_reflection));  
    scene_rgba.rgb += em;
    vec4 clouds_rgba = shade_clouds(surface.view_origin, surface.view_dir, surface.surface_depth);
    vec3 lit_color = light_from_point_light(surface, material, g_moonpos, MOON_COLOR, 0., 1.);
    lit_color *= g_exposure;
    clouds_rgba.rgb *= g_exposure;
 
    composite(scene_rgba, clouds_rgba);
    composite(scene_rgba, vec4(lit_color, 1.));
    return scene_rgba.rgb;
}

vec3 shade_world(SurfaceInfo surface)
{
    vec4 scene_color = vec4(0.);
    if (MATCHES_SURFACE_ID(surface.surface_id, WATER_SURFACE_ID))
    {
        MaterialInfo material;
        material.bump_normal = surface.surface_normal;
        vec3 surface_color = vec3(.07,.08,.1) + mix(vec3(.18, .22, .35), vec3(.21, .25, .38), flow_noise(surface.surface_uv.xxy));
        material.diffuse_color = .6 * surface_color;
        material.emissive_color = .15 * surface_color;
        material.specular_color = .5 * MOON_COLOR;
        material.specular_exponent = 200.;
        material.reflection_intensity = 1.0;
        SurfaceInfo refl_surface = march_scene( surface.surface_point, reflect(surface.view_dir, surface.surface_normal), 0. );
        refl_surface.shade_in_reflection = 1.;
        vec3 refl_color = shade_reflected_world( refl_surface );
        refl_color *= (.5 + .5 * smoothstep(0.3, 2.5, surface.surface_depth));
        refl_color *= (.5 + .5 * smoothstep(3., 1., surface.surface_depth));
        vec3 lit_color = light_from_point_light(surface, material, g_moonpos, MOON_COLOR, 0., 1.);
        lit_color += .2 * refl_color;        
        lit_color *= g_exposure;              
        
        scene_color = vec4(0.);
        composite(scene_color, vec4(lit_color, 1.));
    }
    else
    {
        scene_color.rgb = shade_reflected_world(surface);
    }
    return scene_color.rgb;
}

// --------------------------------------------------------------------------
// MAIN ENTRY POINTS (VERTEX & FRAGMENT)
// --------------------------------------------------------------------------

[shader("vertex")]
VS_Output vertexMain(uint vertexID : SV_VertexID)
{
    static const float2 positions[6] = {
        float2(-1.0, -1.0), // Triangle 1: Top-Left
        float2(-1.0,  1.0), // Triangle 1: Bottom-Left
        float2( 1.0, -1.0), // Triangle 1: Top-Right
        
        float2( 1.0, -1.0), // Triangle 2: Top-Right
        float2(-1.0,  1.0), // Triangle 2: Bottom-Left
        float2( 1.0,  1.0)  // Triangle 2: Bottom-Right
    };

    VS_Output output;
    output.position = float4(positions[vertexID], 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VS_Output input) : SV_Target
{   
    float2 fragCoord = input.position.xy;
    // Vulkan has Y pointing down, Shadertoy expects Y pointing up.
    fragCoord.y = iResolution.y - fragCoord.y;

    setup_globals();
    float denom = TWO_PI / max(1., NUM_AA_SAMPLES - 1.);
    vec3 scene_color = vec3(0.);
    for (float aa = 0.; aa < NUM_AA_SAMPLES; aa += 1.) 
    {
        vec2 aaoffset = step(.5, aa) * .5 * vec2( cos((aa-1.) * denom ), sin((aa-1.) * denom ) );
        CameraInfo camera = setup_camera(aaoffset, fragCoord);
        
        SurfaceInfo surface = march_scene( camera.camera_origin, camera.ray_look_direction, 1. );
        scene_color += shade_world( surface );
    }

    scene_color /= NUM_AA_SAMPLES;

    scene_color *= 1.5;
    scene_color = pow(max(vec3(0.), scene_color), vec3(.8));
    scene_color = mix( scene_color, vec3(dot(scene_color,vec3(0.333))), -.3 );
    scene_color *= vec3(1.1, .9, .9);
    
    vec2 uv = fragCoord.xy / iResolution.xy;
    scene_color *= 0.2 + 0.8 * pow( 8.0 * uv.x * (1.0 - uv.x), 0.1 );
    
    float4 fragColor = float4(1.0);
    if (g_debugcolor.a > 0.) {
        fragColor.rgb = g_debugcolor.rgb;
    } else {
        fragColor.rgb = scene_color;
    }

    return fragColor;
}