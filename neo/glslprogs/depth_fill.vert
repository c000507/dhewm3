#version 450

// Depth pre-pass vertex shader.
// Only transforms position — writes depth, no color output.

layout(push_constant) uniform PushConstants {
	mat4 mvp;	// model-view-projection matrix
} pc;

layout(location = 0) in vec3 inPosition;

void main() {
	gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
