#version 330 core
out vec4 FragColor;
in vec3 FragPos;
uniform vec3 colorTop;
uniform vec3 colorBottom;

void main() {
    // Map y from [-1, 1] to [0, 1]
    float t = FragPos.y * 0.5 + 0.5; 
    vec3 result = mix(colorBottom, colorTop, t);
    FragColor = vec4(result, 1.0);
}