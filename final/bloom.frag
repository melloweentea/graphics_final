#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D scene;     // The original crisp image
uniform sampler2D bloomBlur; // The blurred 'glow' image
uniform float exposure;      // Controls how bright the final scene is

void main() {             
    const float gamma = 2.2;
    vec3 hdrColor = texture(scene, TexCoords).rgb;      
    vec3 bloomColor = texture(bloomBlur, TexCoords).rgb;
    
    // 1. Additive blending: Add the glow on top of the original
    hdrColor += bloomColor; 
    
    // 2. Tone mapping: Converts HDR (0 to 10+) back to LDR (0 to 1) 
    // so your monitor can display it correctly.
    vec3 result = vec3(1.0) - exp(-hdrColor * exposure);
    
    // 3. Gamma correction: Makes the colors look 'natural'
    result = pow(result, vec3(1.0 / gamma));
    
    FragColor = vec4(result, 1.0);
}