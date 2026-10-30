// Port of the GT7 tone mapping operator sample by Polyphony Digital Inc.
// https://blog.selfshadow.com/publications/s2025-shading-course/pdi/supplemental/gt7_tone_mapping.cpp
//
// MIT License
//
// Copyright (c) 2025 Polyphony Digital Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Framebuffer value 1.0 is this many cd/m^2
static const float kReferenceLuminance = 100.0;

/// SDR reference white the curve parameters were fitted for
public static const float kGt7SdrPaperWhite = 250.0 / kReferenceLuminance;

static const float kCurveAlpha = 0.25;
static const float kCurveGrayPoint = 0.538;
static const float kCurveLinearSection = 0.444;
static const float kBlendRatio = 0.6;
static const float kFadeStart = 0.98;
static const float kFadeEnd = 1.16;

float smoothStepRange(float x, float edge0, float edge1)
{
    if (x < edge0)
    {
        return 0.0;
    }
    if (x > edge1)
    {
        return 1.0;
    }
    const float t = (x - edge0) / (edge1 - edge0);
    return t * t * (3.0 - 2.0 * t);
}

struct GtCurve
{
    float peak;
    float midPoint;
    float linearSection;
    float toeStrength;
    float kA;
    float kB;
    float kC;
};

GtCurve makeGtCurve(float peak, float toeStrength)
{
    GtCurve curve;
    curve.peak = peak;
    curve.midPoint = kCurveGrayPoint;
    curve.linearSection = kCurveLinearSection;
    curve.toeStrength = toeStrength;

    const float k = (kCurveLinearSection - 1.0) / (kCurveAlpha - 1.0);
    curve.kA = peak * kCurveLinearSection + peak * k;
    curve.kB = -peak * k * exp(kCurveLinearSection / k);
    curve.kC = -1.0 / (k * peak);
    return curve;
}

/// Toe below the grey point and a convergent shoulder toward the peak
float evaluateGtCurve(GtCurve curve, float x)
{
    if (x < 0.0)
    {
        return 0.0;
    }

    const float weightLinear = smoothStepRange(x, 0.0, curve.midPoint);
    const float weightToe = 1.0 - weightLinear;

    if (x < curve.linearSection * curve.peak)
    {
        const float toeMapped = curve.midPoint * pow(x / curve.midPoint, curve.toeStrength);
        return weightToe * toeMapped + weightLinear * x;
    }
    return curve.kA + curve.kB * exp(x * curve.kC);
}

static const float kPqM1 = 0.1593017578125;
static const float kPqM2 = 78.84375;
static const float kPqC1 = 0.8359375;
static const float kPqC2 = 18.8515625;
static const float kPqC3 = 18.6875;
static const float kPqPeak = 10000.0;

/// Normalised PQ signal to framebuffer value
float eotfSt2084(float n)
{
    n = saturate(n);
    const float np = pow(n, 1.0 / kPqM2);
    const float l = max(np - kPqC1, 0.0) / (kPqC2 - kPqC3 * np);
    return pow(l, 1.0 / kPqM1) * kPqPeak / kReferenceLuminance;
}

/// Framebuffer value to normalised PQ signal
float inverseEotfSt2084(float v)
{
    const float y = max(v, 0.0) * kReferenceLuminance / kPqPeak;
    const float ym = pow(y, kPqM1);
    return exp2(kPqM2 * (log2(kPqC1 + kPqC2 * ym) - log2(1.0 + kPqC3 * ym)));
}

/// Linear Rec.2020 to ICtCp
float3 rgbToICtCp(float3 rgb)
{
    const float l = (rgb.r * 1688.0 + rgb.g * 2146.0 + rgb.b * 262.0) / 4096.0;
    const float m = (rgb.r * 683.0 + rgb.g * 2951.0 + rgb.b * 462.0) / 4096.0;
    const float s = (rgb.r * 99.0 + rgb.g * 309.0 + rgb.b * 3688.0) / 4096.0;

    const float lPq = inverseEotfSt2084(l);
    const float mPq = inverseEotfSt2084(m);
    const float sPq = inverseEotfSt2084(s);

    return float3(
        (2048.0 * lPq + 2048.0 * mPq) / 4096.0,
        (6610.0 * lPq - 13613.0 * mPq + 7003.0 * sPq) / 4096.0,
        (17933.0 * lPq - 17390.0 * mPq - 543.0 * sPq) / 4096.0);
}

float3 iCtCpToRgb(float3 ictcp)
{
    const float l = ictcp.x + 0.00860904 * ictcp.y + 0.11103 * ictcp.z;
    const float m = ictcp.x - 0.00860904 * ictcp.y - 0.11103 * ictcp.z;
    const float s = ictcp.x + 0.560031 * ictcp.y - 0.320627 * ictcp.z;

    const float lLin = eotfSt2084(l);
    const float mLin = eotfSt2084(m);
    const float sLin = eotfSt2084(s);

    return max(
        float3(
            3.43661 * lLin - 2.50645 * mLin + 0.0698454 * sLin,
            -0.79133 * lLin + 1.9836 * mLin - 0.192271 * sLin,
            -0.0259499 * lLin - 0.0989137 * mLin + 1.12486 * sLin),
        float3(0.0, 0.0, 0.0));
}

float3 rec709ToRec2020(float3 c)
{
    return float3(
        dot(float3(0.6274039, 0.3292830, 0.0433131), c),
        dot(float3(0.0690973, 0.9195404, 0.0113623), c),
        dot(float3(0.0163914, 0.0880133, 0.8955953), c));
}

float3 rec2020ToRec709(float3 c)
{
    return float3(
        dot(float3(1.6604910, -0.5876411, -0.0728499), c),
        dot(float3(-0.1245505, 1.1328999, -0.0083494), c),
        dot(float3(-0.0181508, -0.1005789, 1.1187297), c));
}

/// Scene linear Rec.709 where 1.0 is paper white to the same space relative to paper white, at most peak over paper white
/// Both whites are framebuffer values with 1.0 at 100 nits and the peak never goes under the SDR paper white
public float3 gt7ToneMap(float3 scene, float paperWhite, float peak, float toeStrength)
{
    peak = max(peak, kGt7SdrPaperWhite);
    const GtCurve curve = makeGtCurve(peak, toeStrength);
    const float peakUcs = inverseEotfSt2084(peak);

    const float3 rgb = rec709ToRec2020(clamp(scene, 0.0, 65000.0) * paperWhite);
    const float3 ucs = rgbToICtCp(rgb);

    // Per channel curve gives the skewed colour
    const float3 skewedRgb = float3(evaluateGtCurve(curve, rgb.r), evaluateGtCurve(curve, rgb.g), evaluateGtCurve(curve, rgb.b));
    const float3 skewedUcs = rgbToICtCp(skewedRgb);

    // Chroma fades out as the pixel nears the peak
    const float chromaScale = 1.0 - smoothStepRange(ucs.x / peakUcs, kFadeStart, kFadeEnd);
    const float3 scaledRgb = iCtCpToRgb(float3(skewedUcs.x, ucs.y * chromaScale, ucs.z * chromaScale));

    const float3 blended = lerp(skewedRgb, scaledRgb, kBlendRatio);
    return max(rec2020ToRec709(min(blended, float3(peak, peak, peak))), float3(0.0, 0.0, 0.0)) / paperWhite;
}
