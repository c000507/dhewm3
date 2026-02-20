#version 450

// Environment/reflection vertex shader.
// Computes reflection vector for cubemap sampling.

layout(push_constant) uniform PushConstants {
	mat4 mvp;
} pc;

layout(set = 0, binding = 0) uniform EnvParams {
	vec4 viewOrigin;		// camera position in model space
	mat4 modelMatrix;		// model-to-world transform
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 4) in vec3 inNormal;

layout(location = 0) out vec3 outReflect;
layout(location = 1) out vec2 outTexCoord;

void main() {
	gl_Position = pc.mvp * vec4(inPosition, 1.0);

	// Reflection in world space
	vec3 toEye = normalize(viewOrigin.xyz - inPosition);
	vec3 worldNormal = mat3(modelMatrix) * inNormal;
	outReflect = reflect(-toEye, normalize(worldNormal));

	outTexCoord = inTexCoord;
}
