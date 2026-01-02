#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

void main() {
    // Pass the UVs directly to the fragment shader
    TexCoords = aTexCoords;

    // Since ScreenQuad vertices are already -1.0 to 1.0, 
    // we don't need any MVP matrix. We just output the position.
    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
}