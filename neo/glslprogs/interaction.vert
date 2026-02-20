#version 450

// Interaction vertex shader — bump-mapped dynamic lighting.
// Transforms vertex position, computes texture coordinates for
// bump/diffuse/specular maps and light projection, and passes
// tangent-space light and view directions to the fragment shader.

layout(push_constant) uniform PushConstants {
	mat4 mvp;
} pc;

// Per-interaction parameters
layout(set = 0, binding = 0) uniform InteractionParams {
	vec4 lightOrigin;			// light position in model space
	vec4 viewOrigin;			// camera position in model space
	vec4 lightProjectS;			// light projection matrix row S
	vec4 lightProjectT;			// light projection matrix row T
	vec4 lightProjectQ;			// light projection matrix row Q
	vec4 lightFalloffS;			// light falloff texture projection
	vec4 bumpMatrixS;			// bump map texture matrix row S
	vec4 bumpMatrixT;			// bump map texture matrix row T
	vec4 diffuseMatrixS;		// diffuse texture matrix row S
	vec4 diffuseMatrixT;		// diffuse texture matrix row T
	vec4 specularMatrixS;		// specular texture matrix row S
	vec4 specularMatrixT;		// specular texture matrix row T
	vec4 colorModulate;			// vertex color modulation
	vec4 colorAdd;				// vertex color addition
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in vec4 inColor;

layout(location = 0) out vec2 outBumpTC;
layout(location = 1) out vec2 outDiffuseTC;
layout(location = 2) out vec2 outSpecularTC;
layout(location = 3) out vec4 outLightProjectTC;
layout(location = 4) out vec4 outLightFalloffTC;
layout(location = 5) out vec3 outLightDir;		// tangent space
layout(location = 6) out vec3 outViewDir;		// tangent space
layout(location = 7) out vec4 outVertexColor;

void main() {
	gl_Position = pc.mvp * vec4(inPosition, 1.0);

	vec4 pos4 = vec4(inPosition, 1.0);
	vec4 tc4 = vec4(inTexCoord, 0.0, 1.0);

	// Bump/diffuse/specular texture coordinates use UV texcoords with
	// material texture matrix (scroll, scale, rotate).
	outBumpTC.x = dot(bumpMatrixS, tc4);
	outBumpTC.y = dot(bumpMatrixT, tc4);

	outDiffuseTC.x = dot(diffuseMatrixS, tc4);
	outDiffuseTC.y = dot(diffuseMatrixT, tc4);

	outSpecularTC.x = dot(specularMatrixS, tc4);
	outSpecularTC.y = dot(specularMatrixT, tc4);

	// Light projection texture coordinates
	outLightProjectTC.x = dot(lightProjectS, pos4);
	outLightProjectTC.y = dot(lightProjectT, pos4);
	outLightProjectTC.z = 0.0;
	outLightProjectTC.w = dot(lightProjectQ, pos4);

	// Light falloff texture coordinates
	outLightFalloffTC.x = dot(lightFalloffS, pos4);
	outLightFalloffTC.y = 0.5;
	outLightFalloffTC.z = 0.0;
	outLightFalloffTC.w = 1.0;

	// Light direction in tangent space
	vec3 toLight = lightOrigin.xyz - inPosition;
	outLightDir.x = dot(inTangent, toLight);
	outLightDir.y = dot(inBitangent, toLight);
	outLightDir.z = dot(inNormal, toLight);

	// View direction in tangent space
	vec3 toView = viewOrigin.xyz - inPosition;
	outViewDir.x = dot(inTangent, toView);
	outViewDir.y = dot(inBitangent, toView);
	outViewDir.z = dot(inNormal, toView);

	// Vertex color contribution
	outVertexColor = inColor * colorModulate + colorAdd;
}
