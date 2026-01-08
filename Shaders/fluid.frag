#version 330 core
out vec4 FragColor;

uniform float u_time;
uniform vec2 u_resolution;

// Simplex noise functions
vec3 mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec2 mod289(vec2 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec3 permute(vec3 x) { return mod289(((x * 34.0) + 1.0) * x); }

float snoise(vec2 v) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439,
                       -0.577350269189626, 0.024390243902439);
    vec2 i  = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1;
    i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod289(i);
    vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0))
                   + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0, x0), dot(x12.xy, x12.xy),
                            dot(x12.zw, x12.zw)), 0.0);
    m = m * m;
    m = m * m;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

// Fractal Brownian Motion
float fbm(vec2 p, float time) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    
    // Add fluid-like motion by offsetting with time
    vec2 shift = vec2(100.0);
    mat2 rot = mat2(cos(0.5), sin(0.5), -sin(0.5), cos(0.5));
    
    for (int i = 0; i < 6; i++) {
        value += amplitude * snoise(p * frequency + time * 0.1);
        p = rot * p * 2.0 + shift;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}

void main()
{
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    vec2 p = uv * 3.0 - 1.5;
    
    float time = u_time * 0.3;
    
    // Create multiple layers of fluid-like noise
    float n1 = fbm(p + vec2(time * 0.2, time * 0.1), time);
    float n2 = fbm(p + vec2(n1 * 0.5, time * 0.15), time * 1.1);
    float n3 = fbm(p * 1.5 + vec2(n2 * 0.3, n1 * 0.2), time * 0.9);
    
    // Combine noise layers for fluid effect
    float fluid = n1 * 0.5 + n2 * 0.35 + n3 * 0.15;
    fluid = fluid * 0.5 + 0.5; // Normalize to 0-1
    
    // Color palette - deep ocean/aurora inspired
    vec3 color1 = vec3(0.05, 0.02, 0.15);  // Deep purple/black
    vec3 color2 = vec3(0.02, 0.15, 0.35);  // Deep blue
    vec3 color3 = vec3(0.0, 0.45, 0.55);   // Teal
    vec3 color4 = vec3(0.1, 0.7, 0.6);     // Cyan
    vec3 color5 = vec3(0.85, 0.35, 0.55);  // Magenta/pink
    
    // Create smooth color transitions
    float t = fluid;
    vec3 color;
    
    if (t < 0.25) {
        color = mix(color1, color2, t * 4.0);
    } else if (t < 0.5) {
        color = mix(color2, color3, (t - 0.25) * 4.0);
    } else if (t < 0.75) {
        color = mix(color3, color4, (t - 0.5) * 4.0);
    } else {
        color = mix(color4, color5, (t - 0.75) * 4.0);
    }
    
    // Add subtle glow effect based on movement
    float glow = abs(n2 - n1) * 2.0;
    color += vec3(0.1, 0.15, 0.2) * glow;
    
    // Add subtle vignette
    float vignette = 1.0 - length(uv - 0.5) * 0.8;
    color *= vignette;
    
    // Gamma correction
    color = pow(color, vec3(0.9));
    
    FragColor = vec4(color, 1.0);
}


