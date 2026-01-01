#version 330 core

in vec2 uv;
uniform sampler2D textureSampler;

out vec3 finalColor;

void main()
{
	finalColor = texture(textureSampler, uv).rgb;
}
