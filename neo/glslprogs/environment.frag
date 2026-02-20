#version 450

// Environment/reflection fragment shader.
// Samples a cubemap using the reflection vector.

layout(set = 1, binding = 0) uniform samplerCube envMap;

layout(location = 0) in vec3 inReflect;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
	outColor = texture(envMap, inReflect);
}
