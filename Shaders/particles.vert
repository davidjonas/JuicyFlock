#version 430 core

struct Particle
{
    vec4 pos;
    vec4 vel;
    vec4 color;
};

layout (std430, binding = 0) readonly buffer Particles
{
    Particle p[];
};

uniform mat4 u_viewProj;
uniform float u_pointSize;

out vec4 vColor;
out vec2 vDir;

void main()
{
    Particle particle = p[gl_VertexID];
    vec4 clip1 = u_viewProj * vec4 (particle.pos.xyz, 1.0);
    gl_Position = clip1;
    gl_PointSize = u_pointSize;
    vColor = particle.color;

    // Screen-space direction (for the "line" particle shape).
    // We derive it by projecting a small step along the particle velocity direction.
    vec3 vel = particle.vel.xyz;
    float velLen = length (vel);
    vec3 dirW = (velLen > 1.0e-6) ? (vel / velLen) : vec3 (1.0, 0.0, 0.0);
    vec4 clip2 = u_viewProj * vec4 (particle.pos.xyz + dirW * 0.1, 1.0);

    vec2 ndc1 = clip1.xy / max (abs (clip1.w), 1.0e-6);
    vec2 ndc2 = clip2.xy / max (abs (clip2.w), 1.0e-6);
    vec2 d = ndc2 - ndc1;
    float dLen = length (d);
    vDir = (dLen > 1.0e-6) ? (d / dLen) : vec2 (1.0, 0.0);
}


