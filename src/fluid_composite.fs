#version 330
in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;     // accumulated field: rgb = sum(weight * color), a = sum(weight)
uniform vec2 texel;             // 1 / field texture size

// live-tunable (names must match kShaderParamNames in fluid_rendering.cpp)
uniform float threshold;        // field value where the surface sits
uniform float edgeWidth;        // anti-aliasing band, in field texels
uniform float normalSample;     // gradient step, in texels
uniform float bevelWidth;       // raised-rim width, in texels
uniform float normalStrength;   // rim steepness
uniform float outlineWidth;     // dark outline width, in texels
uniform float outlineDarken;
uniform float lightX;
uniform float lightY;
uniform float lightZ;
uniform float shadeStrength;
uniform float specExp;
uniform float specStrength;
uniform float brightness;

void main() {
    // NO blurring of the field anywhere: the edge position comes from the raw field value.
    vec4 f = texture(texture0, fragTexCoord);
    float a = f.a;
    if (a < threshold * 0.25) discard;     // far from the surface

    // field gradient (alpha change per texel) by central differences
    float s = max(normalSample, 1.0);
    vec2 d = texel * s;
    float aL = texture(texture0, fragTexCoord - vec2(d.x, 0.0)).a;
    float aR = texture(texture0, fragTexCoord + vec2(d.x, 0.0)).a;
    float aD = texture(texture0, fragTexCoord - vec2(0.0, d.y)).a;
    float aU = texture(texture0, fragTexCoord + vec2(0.0, d.y)).a;
    vec2 g = vec2(aR - aL, aU - aD) / (2.0 * s);
    float gl = length(g);

    // signed distance (in texels) to the threshold contour: positive inside the fluid.
    // Dividing by the gradient turns any field softness into an exact 1-pixel edge.
    float dist = (a - threshold) / max(gl, 1e-3);

    float mask = clamp(dist / max(edgeWidth, 0.1) + 0.5, 0.0, 1.0);
    if (mask <= 0.0) discard;

    // weighted-average particle color (flat, saturated)
    vec3 base = f.rgb / max(a, 1e-3);

    // raised rim: normal tilts outward near the edge and flattens to straight-up in the interior
    vec2 inward = g / max(gl, 1e-4);
    float t = clamp(dist / max(bevelWidth, 0.01), 0.0, 1.0);
    float slope = clamp(normalStrength * (1.0 - t) * (1.0 - t), 0.0, 0.95);
    vec3 N = vec3(-inward * slope, sqrt(1.0 - slope * slope));

    vec3 L = normalize(vec3(lightX, lightY, lightZ));
    float lit = dot(N, L) - L.z;           // relative to a flat surface: interior stays unchanged

    vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
    float spec = max(pow(max(dot(N, H), 0.0), specExp) - pow(H.z, specExp), 0.0);

    vec3 col = base * brightness * (1.0 + shadeStrength * lit) + vec3(spec * specStrength);

    // crisp dark outline just inside the edge
    float ol = 1.0 - clamp(dist - outlineWidth + 0.5, 0.0, 1.0);
    col = mix(col, col * (1.0 - outlineDarken), ol);

    finalColor = vec4(clamp(col, 0.0, 1.0), mask);   // straight alpha, for raylib's normal blending
}