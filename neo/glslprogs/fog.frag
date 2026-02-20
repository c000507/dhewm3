#version 450

// Fog fragment shader.
// Samples two fog textures (falloff + entry fade) and outputs fog color
// with combined alpha for SRC_ALPHA / ONE_MINUS_SRC_ALPHA blending.

layout(set = 1, binding = 0) uniform sampler2D fogImage;
layout(set = 1, binding = 1) uniform sampler2D fogEnterImage;

layout(location = 0) in vec2 inTexCoord0;
layout(location = 1) in vec2 inTexCoord1;
layout(location = 2) in vec3 inFogColor;

layout(location = 0) out vec4 outColor;

void main() {
	float fogFalloff = texture(fogImage, inTexCoord0).a;
	float fogEnter = texture(fogEnterImage, inTexCoord1).a;

	outColor = vec4(inFogColor, fogFalloff * fogEnter);
}
