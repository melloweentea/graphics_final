#version 330 core

in vec2 UV;
in vec3 FragPos;           
in vec4 FragPosLightSpace; 

uniform vec3 gridColor;
uniform vec3 floorColor;
uniform float gridScale;
uniform sampler2D shadowMap;
uniform vec3 lightPos; 

out vec4 FragColor;

float ShadowCalculation(vec4 fragPosLightSpace) {
    // 1. Perspective divide and transform to [0,1]
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if(projCoords.z > 1.0) return 0.0; // Outside far plane = no shadow

    float currentDepth = projCoords.z;
    float bias = 0.002; 
    float shadow = 0.0;

    // 2. PCF: Sample a 3x3 grid around the target pixel
    // textureSize returns the dimensions of the shadow map (e.g., 1024, 1024)
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r; 
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    
    // 3. Average the 9 samples
    return shadow / 9.0;
}

void main() {
    // --- Grid Logic ---
    vec2 pos = fract(UV * gridScale);
    float thickness = 0.03;
    vec2 grid = smoothstep(thickness, 0.0, pos) + smoothstep(1.0 - thickness, 1.0, pos);
    float lineIntensity = clamp(grid.x + grid.y, 0.0, 1.0);
    vec3 baseColor = mix(floorColor, gridColor, lineIntensity);

    // --- Shadow Logic (returns value between 0.0 and 1.0) ---
    float shadow = ShadowCalculation(FragPosLightSpace);
    
    // 0.7 means shadows are quite dark; 1.0 would be pitch black
    float shadowStrength = 0.7; 
    float lightFactor = 1.0 - (shadow * shadowStrength);

    // --- Final Color & Bloom ---
    // Ambient ensures the floor isn't black in shadows
    float ambient = 0.15;
    vec3 finalColor = baseColor * (ambient + lightFactor);

    if (lineIntensity > 0.0) {
        // BLOOM IMPACT: The glow boost (3.0) is multiplied by lightFactor.
        // In full light: boost is 3.0. In soft PCF edges: boost is 1.5. In deep shadow: boost is ~0.3.
        float glowBoost = 3.0 * lightFactor; 
        FragColor = vec4(finalColor * glowBoost, 1.0);
    } else {
        FragColor = vec4(finalColor, 1.0);
    }
}