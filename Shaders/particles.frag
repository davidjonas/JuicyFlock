#version 430 core

in vec4 vColor;
in vec2 vDir;
out vec4 FragColor;

// 0 = square, 1 = circle, 2 = line (aligned to velocity), 3 = cube (fake shaded sprite)
uniform int u_shape;
uniform float u_alphaMul;

void main()
{
    vec2 p = gl_PointCoord * 2.0 - 1.0; // -1..1 in sprite space

    if (u_shape == 1) // circle
    {
        if (dot (p, p) > 1.0)
            discard;
    }
    else if (u_shape == 2) // line (with a simple arrow head)
    {
        vec2 d = normalize (vDir);
        vec2 n = vec2 (-d.y, d.x);

        float along  = dot (p, d); // -1..1
        float across = dot (p, n); // -1..1

        // Body: thin rectangle
        float halfWidth = 0.16;
        float bodyMin = -0.85;
        float bodyMax =  0.55;
        bool inBody = (abs (across) <= halfWidth) && (along >= bodyMin) && (along <= bodyMax);

        // Head: triangle tapering to the tip
        float headMin = bodyMax;
        float headMax = 1.00;
        float t = clamp ((along - headMin) / max (headMax - headMin, 1.0e-6), 0.0, 1.0);
        float headHalfWidth = mix (halfWidth, 0.0, t);
        bool inHead = (along >= headMin) && (along <= headMax) && (abs (across) <= headHalfWidth);

        if (! (inBody || inHead))
            discard;
    }
    else if (u_shape == 3) // cube-ish shaded sprite (no discard; square footprint)
    {
        // Create a simple 2-face shading split and a couple of "edge" highlights.
        float face = step (0.0, p.x + p.y);        // split along diagonal
        float shade = mix (0.75, 1.10, face);      // darker/lighter face

        float edge = 0.0;
        edge = max (edge, step (0.92, abs (p.x)));
        edge = max (edge, step (0.92, abs (p.y)));
        edge = max (edge, 1.0 - step (0.06, abs (p.x + p.y))); // diagonal ridge

        // Slightly boost edges to read as a cube.
        vec3 rgb = vColor.rgb * shade;
        rgb = mix (rgb, vec3 (1.0), edge * 0.15);
        FragColor = vec4 (rgb, vColor.a * u_alphaMul);
        return;
    }

    FragColor = vec4 (vColor.rgb, vColor.a * u_alphaMul);
}


