#version 450

// Stencil shadow volume vertex shader.
// Extrudes shadow volume vertices to infinity along the light direction.
// Vertices with w=1 are front cap (stay in place), w=0 are extruded.

layout(push_constant) uniform PushConstants {
	mat4 mvp;				// model-view-projection matrix
	vec4 lightOrigin;		// light position in model space
} pc;

layout(location = 0) in vec4 inPosition;	// xyz + w (1=normal, 0=extruded)

void main() {
	vec4 pos = inPosition;

	// If w == 0, extrude to infinity away from light
	if (pos.w == 0.0) {
		// Direction from light to vertex
		vec3 dir = pos.xyz - pc.lightOrigin.xyz;
		// Project to infinity (homogeneous w=0)
		pos = vec4(dir, 0.0);
	}

	gl_Position = pc.mvp * pos;
}
