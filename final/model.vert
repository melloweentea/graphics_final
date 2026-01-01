#version 330 core

// Input
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec2 vertexUV;

// Matrix for vertex transformation
uniform mat4 MVP;

// Output data, to be interpolated for each fragment
out vec2 uv;

void main() {
    // Transform vertex
    gl_Position =  MVP * vec4(vertexPosition, 1);
    
    // Pass vertex UV to the fragment shader
    uv = vertexUV;
}
