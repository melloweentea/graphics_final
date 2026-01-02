#version 330 core

in vec2 UV;
in vec3 vertexColor;

uniform vec3 gridColor;   // Set via glUniform3fv
uniform vec3 floorColor;  // Set via glUniform3fv
uniform float gridScale;  // Set via glUniform1f (e.g., 50.0)

out vec4 FragColor;

void main() {
    // 1. Create the repeating grid coordinates
    // Multiplying UV by gridScale determines how many squares there are
    vec2 pos = fract(UV * gridScale);
    
    // 2. Define line thickness (0.02 is a thin line, 0.1 is thick)
    float thickness = 0.03;
    
    // 3. Create the grid lines using smoothstep for anti-aliasing
    // This checks if we are near the edges (0.0 or 1.0) of the fract cell
    vec2 grid = smoothstep(thickness, 0.0, pos) + smoothstep(1.0 - thickness, 1.0, pos);
    
    // 4. Combine X and Y lines
    float lineIntensity = clamp(grid.x + grid.y, 0.0, 1.0);
    
    // 5. Calculate Final Color
    // Mix the base floor color with the grid color based on intensity
    vec3 finalColor = mix(floorColor, gridColor, lineIntensity);
    
    // 6. Glow/Bloom logic:
    // If the pixel is part of a grid line, we multiply its brightness.
    // Values > 1.0 will be picked up by a Bloom post-processing filter.
    if (lineIntensity > 0.0) {
        FragColor = vec4(finalColor * 3.0, 1.0); // 3.0 is the "glow boost"
    } else {
        FragColor = vec4(finalColor, 1.0);
    }
}