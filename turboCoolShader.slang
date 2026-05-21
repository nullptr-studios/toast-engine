// turboCoolShader.slang

// --------------------------------------------------------------------------
// HARDCODED UNIFORMS & RESOURCES (No Descriptors Required)
// --------------------------------------------------------------------------
static const float  iTime = 15.0; 
static const float3 iResolution = float3(1920.0, 1080.0, 1.0); 
static const float4 iMouse = float4(0.0);

// --------------------------------------------------------------------------
// NATIVE TYPES & COMPILER CLEANUP
// --------------------------------------------------------------------------
#define PI 3.14159265359
#define TWO_PI 6.28318530718

#define REALLY_SMALL_NUMBER 0.0001
#define REALLY_BIG_NUMBER 1000000.

#define BRIDGE_SURFACE_ID 1.
#define ISLAND_SURFACE_ID 2.
#define SKY_SURFACE_ID 3.
#define WATER_SURFACE_ID 4.
#define SUSPENSION_STRUTS_SURFACE_ID 5.
#define BRIDGE_LIGHT_SURFACE_ID 6.
#define MOON_SURFACE_ID 7.

#define MOON_COLOR float3(.91, .93, 1.)

#define DIST_MARCH_STEPS 45
#define DIST_MARCH_MAXDIST 44.

#define MATCHES_SURFACE_ID(id1, id2) (id1 > (id2 - .5)) && (id1 < (id2 + .5))
#define NUM_AA_SAMPLES 8

// Exact math behaviors for domain repetition
float glsl_mod(float x, float y) { return x - y * floor(x / y); }
float2 glsl_mod(float2 x, float2 y) { return x - y * floor(x / y); }
float3 glsl_mod(float3 x, float3 y) { return x - y * floor(x / y); }
float3 glsl_mod(float3 x, float y) { return x - y * floor(x / y); }

// --------------------------------------------------------------------------
// GLOBALS
// --------------------------------------------------------------------------
static float3 g_debugcolor  = float3(0.0);
static float  g_time        = 0.0;
static float  g_exposure    = 1.0;
static float  g_focus       = 0.9;
static float3 g_moonpos     = float3(0.0);
static float3 g_camorigin   = float3(0.0);
static float3 g_campointsat = float3(0.0);

// --------------------------------------------------------------------------
// DATA STRUCTURES
// --------------------------------------------------------------------------
struct VS_Output
{
    float4 position : SV_Position;
};

struct CameraInfo
{
    float3 camera_origin;
    float3 ray_look_direction;
    float3x3 camera_transform;
    float2 image_plane_uv;
};

struct SurfaceInfo
{
    float surface_id;
    float3 view_origin;
    float3 view_dir;
    float3 surface_point;
    float3 surface_normal;
    float2 surface_uv;
    float surface_depth;
    float shade_in_reflection;
};

struct MaterialInfo
{
    float3 bump_normal;
    float3 diffuse_color;
    float3 specular_color; 
    float specular_exponent; 
    float reflection_intensity;
    float3 emissive_color;
};

// --------------------------------------------------------------------------
// HIGH PERFORMANCE BITWISE INTEGER VALUE NOISE (No sin/cos)
// --------------------------------------------------------------------------
float hash(uint n) 
{
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 789221U) + 1376312589U;
    return float(n & 0x7fffffffU) / 2147483648.0;
}

float noise(in float3 x)
{
    float3 p = floor(x);
    float3 f = frac(x);
    f = f * f * (3.0 - 2.0 * f);
    
    uint3 ip = uint3(int3(p));
    uint n = ip.x + ip.y * 157U + 113U * ip.z;
    
    return lerp(
        lerp(lerp(hash(n + 0U),    hash(n + 1U),    f.x),
             lerp(hash(n + 157U),  hash(n + 158U),  f.x), f.y),
        lerp(lerp(hash(n + 113U),  hash(n + 114U),  f.x),
             lerp(hash(n + 270U),  hash(n + 271U),  f.x), f.y), f.z);
}

float flow_noise(in float3 p)
{
    float3 q = p - float3(0., .5 * g_time, 0.);
    float f;
    f  = 0.50000 * noise(q);
    q = q * 3.02 - float3(0., .5 * g_time, 0.);
    f += 0.35000 * noise(q);
    q = q * 3.03;
    f += 0.15000 * noise(q);
    return f;
}

float map4(in float3 p)
{
    float3 q = p;
    float f;
    f  = 0.50000 * noise(q); q = q * 2.02;
    f += 0.25000 * noise(q); q = q * 2.03;
    f += 0.12500 * noise(q); q = q * 2.01;
    f += 0.06250 * noise(q);
    return f;
}

// --------------------------------------------------------------------------
// DISTANCE FIELDS & ACCELERATION STRUCTURES
// --------------------------------------------------------------------------
float sphere_df(float3 p, float r) { return length(p) - r; }
float roundbox_df(float3 p, float3 b, float r) { return length(max(abs(p - float3(0., .5 * b.y, 0.)) - .5 * b, 0.0)) - r; }

float2 mergeobjs(float2 a, float2 b) { return lerp(b, a, step(a.x, b.x)); }
float uniondf(float a, float b) { return min(a, b); }

float2 scene_df(float3 p)
{
    // Bounding-box acceleration: Check proximity before computing 4-octave FBM noise
    float3 gp_base = p * float3(.2, 1., .2) + float3(7.2, 3.3, -2.);
    float base_dist = sphere_df(gp_base, 2.9);
    
    // Only pay the ALU cost if the ray is within proximity boundary
    if (base_dist < 0.8) 
    {
        float3 gp_noise = p * float3(.2, 1., .2) + float3(0., 10., 0.) * (.5 * map4(.2 * p) - .5) + float3(7.2, 3.3, -2.);
        base_dist = sphere_df(gp_noise, 2.9);
    }
    float2 ground_obj = float2(base_dist, ISLAND_SURFACE_ID);    

    float3 mp = p - g_moonpos;
    float mdf = sphere_df(mp, 1.2);
    
    float3 bb = p;
    bb.x = glsl_mod(bb.x + 4., 8.) - 4.;
    bb.z = abs(bb.z) - .15;
    float bdf = roundbox_df(bb, float3(.1, 2.5, .03), .01);
    bdf = uniondf(bdf, roundbox_df(bb, float3(.35, .12, .15), .01));
    bdf = uniondf(bdf, roundbox_df(bb - float3(0., 2.45, -0.1), float3(.11, .1, .3), .01));
    bdf = uniondf(bdf, roundbox_df(bb - float3(0., .92, -0.1), float3(8., .02, .2), .01));
    bdf = uniondf(bdf, roundbox_df(bb - float3(0., 1.05, -0.1), float3(8., .02, .2), .01));
                 
    float3 cb = bb;
    cb.y = abs(glsl_mod(cb.y + .19, .38) - .19);
    float m = step(2.5, bb.y) + step(.95, bb.y) * step(bb.y, 1.3);
    cb.y -= .15 + REALLY_BIG_NUMBER * m;
    
    cb.yz = mul(float2x2(-.707, -.707, .707, -.707), cb.yz);
    bdf = uniondf(bdf, roundbox_df(cb, float3(.03, 1., .03), .01)); 

    float3 sp = p;
    sp.x = glsl_mod(sp.x, 8.) - 4.;
    sp.z = abs(sp.z) - .15;
    sp.y -= .09 * sp.x * sp.x;
    bdf = uniondf(bdf, roundbox_df(sp - float3(0., 1.1, 0.), float3(8., .01, .01), .01));
        
    float3 tp = p;
    tp.x = glsl_mod(tp.x + .05, .1) - .05;
    tp.z -= .13;
    float sdf = roundbox_df(tp - float3(0., 1.06, 0.), float3(.0, .089 * sp.x * sp.x + .02, .0), .005);
    float wldf = sphere_df(bb - float3(0.0, 2.64, -.15), .03);
    
    float2 bridge_obj = float2(bdf, BRIDGE_SURFACE_ID); 
    float2 suspension_obj = float2(sdf, SUSPENSION_STRUTS_SURFACE_ID);
    float2 light_obj = float2(wldf, BRIDGE_LIGHT_SURFACE_ID); 
    float2 moon_obj = float2(mdf, MOON_SURFACE_ID);
    
    float2 obj = ground_obj;
    obj = mergeobjs(obj, bridge_obj);
    obj = mergeobjs(obj, suspension_obj);
    obj = mergeobjs(obj, light_obj);
    obj = mergeobjs(obj, moon_obj);
    return obj;
}

// Optimized Tetrahedral configuration: 4 samples instead of 6
float3 calc_normal(float3 p)
{
    const float2 k = float2(1.0, -1.0);
    const float eps = 0.005;
    
    return normalize( 
        k.xyy * scene_df(p + k.xyy * eps).x + 
        k.yyx * scene_df(p + k.yyx * eps).x + 
        k.yxy * scene_df(p + k.yxy * eps).x + 
        k.xxx * scene_df(p + k.xxx * eps).x 
    );
}

// --------------------------------------------------------------------------
// RENDERING PIPELINE FUNCTIONS
// --------------------------------------------------------------------------
float3 rotate_xaxis(float3 point_val, float cosa, float sina)
{
    return float3(point_val.x,
                  point_val.y * cosa - point_val.z * sina,
                  point_val.y * sina + point_val.z * cosa);
}

float3 rotate_yaxis(float3 point_val, float cosa, float sina)
{
    return float3(point_val.x * cosa  + point_val.z * sina,
                  point_val.y,
                  point_val.x * -sina + point_val.z * cosa);
}

float2 intersect_plane(float3 ro, float3 rd, float3 pn, float3 po)
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
    return float2(intersected, t);
}

float3 intersect_sphere(float3 ro, float3 rd, float sphr, float3 sphc)
{
    float3 oro = ro - sphc;
    float a = dot(rd, rd);
    float b = dot(oro, rd);
    float c = dot(oro, oro) - sphr * sphr;
    float discr = b * b - a * c; 
    
    float sdisc = sqrt(max(0.0, discr));
    float tmin = (-b - sdisc) / a;
    float tmax = (-b + sdisc) / a; 

    float hit = step(0., tmin);
    float outside = step(sphr * sphr, dot(oro, oro));
    return outside * float3(hit, tmin, tmax);
}

float3 intersect_isphere(float3 ro, float3 rd, float sphr, float3 sphc)
{
    float3 oro = ro - sphc;
    float a = dot(rd, rd);
    float b = dot(oro, rd);
    float c = dot(oro, oro) - sphr * sphr;
    float discr = b * b - a * c; 

    float sdisc = sqrt(max(0.0, discr));
    float tmin = (-b - sdisc) / a;
    float tmax = (-b + sdisc) / a; 

    float hit = step(0., tmax);
    return float3(hit, tmin, tmax);
}

void composite(inout float4 ci, float4 ca)
{
    ci += ca * (1. - ci.a);
}

float3 nearest_point_on_line(float3 a, float3 b, float3 p)
{
    float3 ba = b - a;
    float t = dot(ba, (p - a)) / dot(ba, ba);
    return a + t * ba;
}

float periodicmix(float v1, float v2, float v3, float x)
{
    float modx = glsl_mod(x, 3.);
    return lerp(v1, 
                lerp(v2, 
                    lerp(v3, v1, smoothstep(.5, .6, modx - 2.)), 
                    smoothstep(.5, .6, modx - 1.)), 
                smoothstep(.5, .6, modx));
}

void setup_globals()
{
    g_time = 1. * iTime;
    g_exposure = 1.;
    
    g_camorigin = float3(0.0, .1, 7.0);
    float rotxang = -.02 * cos(.02 * g_time) - .03;
    g_camorigin = rotate_xaxis(g_camorigin, cos(rotxang), sin(rotxang));
    
    float rotyang = PI * .28 + .05 * sin(.2 * g_time);
    g_camorigin = rotate_yaxis(g_camorigin, cos(rotyang), sin(rotyang));
    g_campointsat = float3(0., .55, 1.5);
    
    float3 iu = float3(0., 1., 0.);
    float3 iz = normalize(g_campointsat - g_camorigin);
    float3 ix = normalize(cross(iz, iu));
    float3 iy = cross(ix, iz);
    
    float inv_aspect_ratio = iResolution.y / iResolution.x;
    float2 click_uv = iMouse.xy / iResolution.xy - .5;
    click_uv.y *= inv_aspect_ratio;
    float3 ray_click_dir = normalize(click_uv.x * ix + click_uv.y * iy + g_focus * iz);
    float3 result = intersect_sphere(g_camorigin, ray_click_dir, 40., g_camorigin);

    g_moonpos = (iMouse.x + iMouse.y > 1.) ? (g_camorigin + result.z * ray_click_dir) : float3(-28., 8., 3.);
}

CameraInfo setup_camera(float2 aaoffset, float2 fragCoord)
{
    float inv_aspect_ratio = iResolution.y / iResolution.x;
    float2 image_plane_uv = (fragCoord.xy + aaoffset) / iResolution.xy - .5;
    image_plane_uv.y *= inv_aspect_ratio;
    
    float3 iu = float3(0., 1., 0.);
    float3 iz = normalize(g_campointsat - g_camorigin);
    float3 ix = normalize(cross(iz, iu));
    float3 iy = cross(ix, iz);
    float3 ray_look_direction = normalize(image_plane_uv.x * ix + image_plane_uv.y * iy + g_focus * iz);
    
    CameraInfo info;
    info.camera_origin = g_camorigin;
    info.ray_look_direction = ray_look_direction;
    info.camera_transform = float3x3(ix, iy, iz); 
    info.image_plane_uv = image_plane_uv;
    
    return info;
}

float2 intersect_water(float3 ro, float3 rd) { return intersect_plane(ro, rd, float3(0., 1., 0.), float3(0., 0., 0.)); }

SurfaceInfo march_scene(float3 ray_origin, float3 ray_direction, float consider_water)
{
    SurfaceInfo surface;
    surface.surface_id = -1.;
    surface.view_origin = ray_origin;
    surface.view_dir = ray_direction;
    surface.surface_point = float3(0.);
    surface.surface_normal = float3(0.);
    surface.surface_uv = float2(0.);
    surface.surface_depth = 0.;
    surface.shade_in_reflection = 0.;
    float2 water = consider_water * intersect_water(ray_origin, ray_direction);
    
    const float epsilon = 0.001;
    float dist = 10. * epsilon;
    float total_t = 0.;
    float curr_t = 0.;
    
    float3 ro = ray_origin;
    float3 rd = ray_direction;
    
    for (int i = 0; i < DIST_MARCH_STEPS; i++) 
    {
        if (abs(dist) < epsilon || curr_t + total_t > DIST_MARCH_MAXDIST) break;
        float3 p = ro + curr_t * rd;
        float2 dfresult = scene_df(p);
        float2 sky_obj = float2(-sphere_df(p, 35.), SKY_SURFACE_ID);
        dfresult = mergeobjs(sky_obj, dfresult);

        dist = dfresult.x;        
        curr_t += dist;
        surface.surface_id = dfresult.y;
        if (water.x > .5 && curr_t > water.y)
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
        float3 n = float3(0., 1., 0.);
        float3 u = normalize(-float3(1., 0., 1.) * ray_origin);
        float3 v = cross(n, u);
        surface.surface_uv = float2(100., 10.) * float2(dot(surface.surface_point, u), dot(surface.surface_point, v));
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
        surface.surface_normal = calc_normal(surface.surface_point);
        surface.surface_uv = surface.surface_point.xz;
    }
            
    surface.surface_depth = total_t;
    return surface;
}

float3 light_from_point_light(SurfaceInfo surface, MaterialInfo material, float3 light_position, float3 light_color, float falloff_with_distance, float specular_sharpen)
{
    float3 light_direction = normalize(light_position - surface.surface_point);
    float3 light_reflection_direction = reflect(light_direction, material.bump_normal);
    float3 reflective_shading = material.specular_color * pow(max(0., dot(light_reflection_direction, surface.view_dir)), material.specular_exponent * specular_sharpen);
    float ldist = length(surface.surface_point - light_position);
    float dist_atten = 1. / ldist;
    float3 diffuse_shading = material.diffuse_color * max(0., dot(light_direction, material.bump_normal)) * lerp(1., dist_atten, falloff_with_distance);    
    return light_color * (diffuse_shading + reflective_shading);
}

// Loop completely unwrapped via unroll attribute
float4 shade_clouds(float3 ro, float3 rd, float depth)
{
    float4 cloud_rgba = float4(0.);
    
    [unroll]
    for (int i = 0; i < 3; i++)
    {
        float fi = float(i);
        float3 ch = intersect_isphere(ro, rd, 3.1 * fi + 4.5, ro);
        if (ch.x > .5 && ch.z < depth)
        {
            float3 hp = ro + rd * ch.z;
            float3 hpo = nearest_point_on_line(ro, g_moonpos, hp);
            float3 uvhp = hp - float3(-28., 9., 9.);
            uvhp *= 2.;
            float height_s = smoothstep(1., 5., hp.y);
            float cloud_alpha = (.06 + .02 * fi) * (.2 + .8 * smoothstep(.1, .9 - .25 * height_s, map4(float3(1., .8, 0.) * uvhp.yxz + float3(3. * fi, 5. * fi , .05 * g_time))));
            cloud_alpha *= smoothstep(0.5, 1.5, hp.y) * smoothstep(2. + 1.8 * fi, .0, hp.y);
            float3 cloud_color = 2. * float3(.6, .8, 1. + .1 * fi);
            float3 halod = hp - hpo;
            float3 halo_color = .8 * MOON_COLOR * max(0., 4. - length(2. * halod));
            cloud_color += .6 * pow(halo_color, float3(2.));
            composite(cloud_rgba, float4(cloud_color * cloud_alpha, cloud_alpha));            
        }
    }
    return cloud_rgba;
}

float3 shade_reflected_world(SurfaceInfo surface)
{
    MaterialInfo material;
    material.bump_normal = surface.surface_normal;
    material.diffuse_color = float3(0.);
    material.specular_color = float3(0.);
    material.specular_exponent = 1.;
    material.reflection_intensity = 1.;
    material.emissive_color = float3(0.);

    if (MATCHES_SURFACE_ID(surface.surface_id, BRIDGE_SURFACE_ID))
    {
        material.diffuse_color = .02 * float3(.65, .62, .68);
        material.specular_color = .5 * float3(0.5, 0.6, 0.7);
        material.specular_exponent = 30.;
        material.emissive_color = float3(.04, .03, .035);
    } 
    else if (MATCHES_SURFACE_ID(surface.surface_id, BRIDGE_LIGHT_SURFACE_ID))
    {
        float i = abs(sin(g_time));
        material.emissive_color = i * i * float3(2., .05, .05);
        material.reflection_intensity = 10.;
    }
    else if (MATCHES_SURFACE_ID(surface.surface_id, SUSPENSION_STRUTS_SURFACE_ID))
    {
        float3 dp = surface.surface_point.xyz;
        dp.x = glsl_mod(dp.x + 4., 8.) - 4.;
        float s = (.5 * cos(6. * dp.y * pow(abs(dp.x), .5) - 4. * g_time) + .5);
        float c1 = smoothstep(.3, 1., s);

        s = (.5 * cos(4. * dp.y * dp.x + lerp(-1., 1., step(0., dp.x)) * 5. * g_time) + .5);
        float c2 = smoothstep(.2, .8, s);
           
        dp = surface.surface_point.xyz;
        s = .5 * cos(2. * dp.x - 2. * g_time + 5. * 1. * sin(3. * dp.y + 1. * g_time)) + .5;
        float c3 = smoothstep(.2, .9, s);
        
        float c = periodicmix(c1, c2, c3, .2 * g_time + .3 * dp.y);
        float l = glsl_mod(20. * dp.y, 1.);
        c *= smoothstep(0.2, .5, l) * smoothstep(.8, .5, l);
        material.emissive_color = 2. * c * float3(.9, .9, 1.);
        material.reflection_intensity = 8.;
    } 
    else if (MATCHES_SURFACE_ID(surface.surface_id, ISLAND_SURFACE_ID))
    {
        float3 surface_color = .05 * float3(1., .5, .7);
        material.diffuse_color = surface_color;
        material.emissive_color = .1 * surface_color;
        material.specular_color = .1 * surface_color;
    }
    else if (MATCHES_SURFACE_ID(surface.surface_id, SKY_SURFACE_ID))
    {
        float3 sky_color = .6 * float3(0.15, 0.12, .12) * smoothstep(3., 0., surface.surface_point.y);
        float l = smoothstep(2., 0., surface.surface_point.y);
        sky_color += .5 * float3(0.2, 0.14, .12) * l * l;
        float3 cn = surface.surface_point;
        cn.z = floor(20. * cn.z) * .05;
        float city_noise = noise(8. * cn.xzz);
        sky_color += 2. * float3(1., .8, .7) * smoothstep(.25 * city_noise - .05, -.06, surface.surface_point.y);
        material.emissive_color = sky_color;
    }
    else if (MATCHES_SURFACE_ID(surface.surface_id, MOON_SURFACE_ID))
    {
        float3x3 om = float3x3(0.00,  0.80,  0.60,
                               -0.80,  0.36, -0.48,
                               -0.60, -0.48,  0.64);
        float3 mp = 1.3 * mul(om, (surface.surface_point - g_moonpos));

        float moon = .1;
        moon += .6 * smoothstep(0.2,  .8, noise(.6 * (mp - float3(1., 0., 0.))));
        mp = 1.2 * mul(om, mp); 
        moon += .5 * smoothstep(0.2,  1., noise(.8 * (mp - float3(1., 0., 0.))));
        mp = 2.8 * mul(om, mp); 
        moon += 0.300 * smoothstep(0.1,  1.3, pow(noise(mp), 3.0));
        moon *= .8 + .2 * smoothstep(0.5,  0.7, noise(1.7 * mp));
        moon *= .6 + .4 * noise(30.5 * mp); 
        moon += 0.35 * pow(noise(15.5 * mp), 6.);
        material.emissive_color = moon * .6 * MOON_COLOR;
        material.reflection_intensity = 4.;
    }
      
    float4 scene_rgba = float4(0.05, .05, .08, 0.1);
    float3 em = material.emissive_color * lerp(1., material.reflection_intensity, step(0.5, surface.shade_in_reflection));  
    scene_rgba.rgb += em;
    float4 clouds_rgba = shade_clouds(surface.view_origin, surface.view_dir, surface.surface_depth);
    float3 lit_color = light_from_point_light(surface, material, g_moonpos, MOON_COLOR, 0., 1.);
    lit_color *= g_exposure;
    clouds_rgba.rgb *= g_exposure;
 
    composite(scene_rgba, clouds_rgba);
    composite(scene_rgba, float4(lit_color, 1.));
    return scene_rgba.rgb;
}

float3 shade_world(SurfaceInfo surface)
{
    float4 scene_color = float4(0.);
    if (MATCHES_SURFACE_ID(surface.surface_id, WATER_SURFACE_ID))
    {
        MaterialInfo material;
        material.bump_normal = surface.surface_normal;
        float3 surface_color = float3(.07,.08,.1) + lerp(float3(.18, .22, .35), float3(.21, .25, .38), flow_noise(surface.surface_uv.xxy));
        material.diffuse_color = .6 * surface_color;
        material.emissive_color = .15 * surface_color;
        material.specular_color = .5 * MOON_COLOR;
        material.specular_exponent = 200.;
        material.reflection_intensity = 1.0;
        SurfaceInfo refl_surface = march_scene(surface.surface_point, reflect(surface.view_dir, surface.surface_normal), 0.);
        refl_surface.shade_in_reflection = 1.;
        float3 refl_color = shade_reflected_world(refl_surface);
        refl_color *= (.5 + .5 * smoothstep(0.3, 2.5, surface.surface_depth));
        refl_color *= (.5 + .5 * smoothstep(3., 1., surface.surface_depth));
        float3 lit_color = light_from_point_light(surface, material, g_moonpos, MOON_COLOR, 0., 1.);
        lit_color += .2 * refl_color;        
        lit_color *= g_exposure;              
        
        scene_color = float4(0.);
        composite(scene_color, float4(lit_color, 1.));
    }
    else
    {
        scene_color.rgb = shade_reflected_world(surface);
    }
    return scene_color.rgb;
}

// --------------------------------------------------------------------------
// MAIN ENTRY POINTS
// --------------------------------------------------------------------------
[shader("vertex")]
VS_Output vertexMain(uint vertexID : SV_VertexID)
{
    static const float2 positions[6] = {
        float2( 1.0,  1.0),
        float2(-1.0,  1.0),
        float2( 1.0, -1.0),
        
        float2( 1.0, -1.0),
        float2(-1.0,  1.0),
        float2(-1.0, -1.0) 
    };

    VS_Output output;
    output.position = float4(positions[vertexID], 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VS_Output input) : SV_Target
{   
    float2 fragCoord = input.position.xy;
    fragCoord.y = iResolution.y - fragCoord.y;

    setup_globals();
    float denom = TWO_PI / max(1., float(NUM_AA_SAMPLES - 1));
    float3 scene_color = float3(0.);
    
    for (int aa = 0; aa < NUM_AA_SAMPLES; aa++) 
    {
        float faa = float(aa);
        float2 aaoffset = step(.5, faa) * .5 * float2(cos((faa - 1.) * denom), sin((faa - 1.) * denom));
        CameraInfo camera = setup_camera(aaoffset, fragCoord);
        
        SurfaceInfo surface = march_scene(camera.camera_origin, camera.ray_look_direction, 1.);
        scene_color += shade_world(surface);
    }

    scene_color /= float(NUM_AA_SAMPLES);

    scene_color *= 1.5;
    scene_color = pow(max(float3(0.), scene_color), float3(.8));
    scene_color = lerp(scene_color, float3(dot(scene_color, float3(0.333))), -.3);
    scene_color *= float3(1.1, .9, .9);
    
    float2 uv = fragCoord.xy / iResolution.xy;
    scene_color *= 0.2 + 0.8 * pow(8.0 * uv.x * (1.0 - uv.x), 0.1);
    
    float4 fragColor = float4(1.0);
    if (g_debugcolor.x + g_debugcolor.y + g_debugcolor.z > 0.) {
        fragColor.rgb = g_debugcolor.rgb;
    } else {
        fragColor.rgb = scene_color;
    }

    return fragColor;
}