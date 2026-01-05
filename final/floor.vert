#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec2 aUV;

uniform mat4 MVP;              // cameraMatrix * modelMatrix
uniform mat4 model;            // Just the modelMatrix
uniform mat4 lightSpaceMatrix; // The Sun's View-Projection
uniform vec3 playerPos;       // Player position for effects

out vec2 UV;
out vec3 vertexColor;
out vec3 FragPos;
out vec4 FragPosLightSpace;

void main() {
    // Standard screen position 
    gl_Position = MVP * vec4(aPos, 1.0);
    
    // World position for lighting/shadow math
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = vec3(worldPos);
    
    // Position from the Sun's perspective
    FragPosLightSpace = lightSpaceMatrix * worldPos;

    UV = aUV + vec2(playerPos.z, playerPos.x) * 0.1;
    vertexColor = aColor; 
}