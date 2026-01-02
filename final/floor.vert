#version 330 core

layout (location = 0) in vec3 aPos;   // From vertexBufferID
layout (location = 1) in vec3 aColor; // From colorBufferID
layout (location = 2) in vec2 aUV;    // From uvBufferID

uniform mat4 MVP;

out vec2 UV;
out vec3 vertexColor;

void main() {
    gl_Position = MVP * vec4(aPos, 1.0);
    
    // Pass data to fragment shader
    UV = aUV;
    vertexColor = aColor; 
}