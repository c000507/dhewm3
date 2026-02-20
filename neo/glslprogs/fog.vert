#version 450

// Fog vertex shader.
// Computes texture coordinates from fog planes (replacing GL_OBJECT_PLANE texgen).
// Four fog planes define how fog density and entry fade vary across surfaces.

layout(push_constant) uniform PushConstants {
	mat4 mvp;
} pc;

layout(set = 0, binding = 0) uniform FogParams {
	vec4 fogPlane0;   // S for fogImage (local-space falloff plane, +0.5 offset)
	vec4 fogPlane1;   // T for fogImage (always (0,0,0,0.5))
	vec4 fogPlane2;   // T for fogEnterImage (local-space terminator fade plane)
	vec4 fogPlane3;   // S for fogEnterImage (view-origin constant offset)
	vec4 fogColor;    // RGB = fog color
};

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec2 outTexCoord0;  // fogImage texcoords
layout(location = 1) out vec2 outTexCoord1;  // fogEnterImage texcoords
layout(location = 2) out vec3 outFogColor;

void main() {
	gl_Position = pc.mvp * vec4(inPosition, 1.0);

	// Compute texture coordinates via fog plane dot products
	// (equivalent to GL_OBJECT_PLANE texgen)
	vec4 pos4 = vec4(inPosition, 1.0);

	// Texture 0: fog falloff image
	outTexCoord0.x = dot(fogPlane0, pos4);  // S: distance-based falloff
	outTexCoord0.y = dot(fogPlane1, pos4);  // T: constant 0.5

	// Texture 1: fog enter correction image
	outTexCoord1.x = dot(fogPlane3, pos4);  // S: view-origin offset
	outTexCoord1.y = dot(fogPlane2, pos4);  // T: terminator fade

	outFogColor = fogColor.rgb;
}
