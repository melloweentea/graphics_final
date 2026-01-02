#version 330 core

in vec2 UV;
in vec3 vertexColor;
in vec3 FragPos;           
in vec4 FragPosLightSpace; // Position relative to the sun

uniform vec3 gridColor;
uniform vec3 floorColor;
uniform float gridScale;

// Shadow Uniforms
uniform sampler2D shadowMap;
uniform vec3 lightPos; // The sun's position in world space

out vec4 FragColor;

float ShadowCalculation(vec4 fragPosLightSpace) {
    // 1. Perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    // 2. Transform to [0,1] range to match texture coordinates
    projCoords = projCoords * 0.5 + 0.5;
    
    // 3. Get closest depth from shadow map
    float closestDepth = texture(shadowMap, projCoords.xy).r; 
    
    // 4. Get current depth from sun's perspective
    float currentDepth = projCoords.z;
    
    // 5. Calculate bias to fix shadow acne
    // We use a small offset so the floor doesn't shadow itself
    float bias = 0.002; 
    
    // 6. Check if pixel is in shadow
    float shadow = currentDepth - bias > closestDepth  ? 1.0 : 0.0;
    
    // Keep area outside the light's frustum lit
    if(projCoords.z > 1.0) shadow = 0.0;
    
    return shadow;
}

void main() {
    // --- Existing Grid Logic ---
    vec2 pos = fract(UV * gridScale);
    float thickness = 0.03;
    vec2 grid = smoothstep(thickness, 0.0, pos) + smoothstep(1.0 - thickness, 1.0, pos);
    float lineIntensity = clamp(grid.x + grid.y, 0.0, 1.0);
    vec3 baseColor = mix(floorColor, gridColor, lineIntensity);

    // --- Shadow Logic ---
    float shadow = ShadowCalculation(FragPosLightSpace);
    
    // We define how dark the shadows are (0.3 means shadows are 30% brightness)
    float shadowIntensity = 0.4; 
    // If in shadow, we multiply the color by a fraction (dim it)
    // If not in shadow (shadow=0), we multiply by 1.0 (full brightness)
    vec3 finalColor = baseColor * (1.0 - (shadow * (1.0 - shadowIntensity)));

    // --- Glow/Bloom Logic ---
    if (lineIntensity > 0.0) {
        // If the grid line is in shadow, we reduce its glow too
        float glowFactor = (lineIntensity > 0.0) ? 3.0 : 1.0;
        FragColor = vec4(finalColor * glowFactor, 1.0);
    } else {
        FragColor = vec4(finalColor, 1.0);
    }
}

//debug main
// void main() {
//     vec3 projCoords = FragPosLightSpace.xyz / FragPosLightSpace.w;
//     projCoords = projCoords * 0.5 + 0.5;
//     float depthValue = texture(shadowMap, projCoords.xy).r;
    
//     // This will show the depth map. 
//     // If the floor is pure white, the Sun isn't seeing any objects.
//     // If you see black silhouettes, your Shadow Map is working, and the issue is your Matrix math.
//     FragColor = vec4(vec3(depthValue), 1.0);
// }