#version 450

// General-purpose vertex shader for GUI, ambient stages, decals, particles.
// Transforms position via MVP, applies optional texture matrix, and
// passes through texture coordinates and vertex color.

layout(push_constant) uniform PushConstants {
	mat4 mvp;
	vec4 texMatrixS;	// texture matrix row S: (s_scale, s_rot, 0, s_trans)
	vec4 texMatrixT;	// texture matrix row T: (t_rot, t_scale, 0, t_trans)
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 5) in vec4 inColor;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec4 outColor;

void main() {
	gl_Position = pc.mvp * vec4(inPosition, 1.0);

	vec4 tc4 = vec4(inTexCoord, 0.0, 1.0);
	outTexCoord.x = dot(pc.texMatrixS, tc4);
	outTexCoord.y = dot(pc.texMatrixT, tc4);
	outColor = inColor;
}
