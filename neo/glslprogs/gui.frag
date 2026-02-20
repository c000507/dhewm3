#version 450

// General-purpose shader pass fragment shader.
// Used for GUI rendering, ambient material stages, decals, particles, etc.
// Samples a texture, multiplies by vertex color and stage color,
// and applies alpha test if configured.

layout(set = 1, binding = 0) uniform sampler2D diffuseMap;

layout(set = 0, binding = 1) uniform FragParams {
	vec4 color;				// stage color modulation
	vec4 alphaTestParams;	// x = alphaRef, y = alphaTestMode (0=none, 1=lt, 2=ge, 3=eq)
							// z = vertColorMode (0=ignore, 1=modulate, 2=inverse modulate)
};

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
	vec4 texColor = texture(diffuseMap, inTexCoord);

	// Vertex color handling (replaces fixed-function GL TexEnv modes)
	int vcMode = int(alphaTestParams.z + 0.5);
	vec4 vertColor;
	if (vcMode == 1) {
		vertColor = inColor;					// SVC_MODULATE
	} else if (vcMode == 2) {
		vertColor = vec4(1.0) - inColor;		// SVC_INVERSE_MODULATE
	} else {
		vertColor = vec4(1.0);					// SVC_IGNORE
	}

	outColor = texColor * vertColor * color;

	// Alpha test (replaces fixed-function glAlphaFunc)
	int mode = int(alphaTestParams.y + 0.5);
	if (mode == 1) {
		// GLS_ATEST_LT_128: keep if alpha < ref, discard otherwise
		if (outColor.a >= alphaTestParams.x) discard;
	} else if (mode == 2) {
		// GLS_ATEST_GE_128: keep if alpha >= ref, discard otherwise
		if (outColor.a < alphaTestParams.x) discard;
	} else if (mode == 3) {
		// GLS_ATEST_EQ_255: keep if alpha == ref (with tolerance)
		if (abs(outColor.a - alphaTestParams.x) > 0.01) discard;
	}
}
