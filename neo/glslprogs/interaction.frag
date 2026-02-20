#version 450

// Interaction fragment shader — bump-mapped dynamic lighting.
// Computes per-pixel diffuse and specular lighting from a single light,
// using normal maps, light projection/falloff textures, and a specular
// lookup table.

layout(set = 0, binding = 0) uniform InteractionParams {
	vec4 lightOrigin;
	vec4 viewOrigin;
	vec4 lightProjectS;
	vec4 lightProjectT;
	vec4 lightProjectQ;
	vec4 lightFalloffS;
	vec4 bumpMatrixS;
	vec4 bumpMatrixT;
	vec4 diffuseMatrixS;
	vec4 diffuseMatrixT;
	vec4 specularMatrixS;
	vec4 specularMatrixT;
	vec4 colorModulate;
	vec4 colorAdd;
};

layout(set = 0, binding = 1) uniform FragParams {
	vec4 diffuseColor;			// light diffuse color
	vec4 specularColor;			// light specular color
	vec4 gammaBrightness;		// {brightness, brightness, brightness, 1/gamma}
};

// Textures
layout(set = 1, binding = 0) uniform sampler2D bumpMap;			// normal map
layout(set = 1, binding = 1) uniform sampler2D lightFalloff;	// light attenuation
layout(set = 1, binding = 2) uniform sampler2D lightProjection;	// projected light pattern
layout(set = 1, binding = 3) uniform sampler2D diffuseMap;		// surface color
layout(set = 1, binding = 4) uniform sampler2D specularMap;		// specular intensity
layout(set = 1, binding = 5) uniform sampler2D specularTable;	// specular lookup

layout(location = 0) in vec2 inBumpTC;
layout(location = 1) in vec2 inDiffuseTC;
layout(location = 2) in vec2 inSpecularTC;
layout(location = 3) in vec4 inLightProjectTC;
layout(location = 4) in vec4 inLightFalloffTC;
layout(location = 5) in vec3 inLightDir;
layout(location = 6) in vec3 inViewDir;
layout(location = 7) in vec4 inVertexColor;

layout(location = 0) out vec4 outColor;

void main() {
	// Normalize tangent-space vectors
	vec3 lightDir = normalize(inLightDir);
	vec3 viewDir = normalize(inViewDir);

	// Sample and decode normal map (0..1 -> -1..1)
	vec3 normal = texture(bumpMap, inBumpTC).rgb * 2.0 - 1.0;
	normal = normalize(normal);

	// Diffuse: N dot L
	float NdotL = clamp(dot(normal, lightDir), 0.0, 1.0);

	// Half-angle for specular
	vec3 halfAngle = normalize(lightDir + viewDir);
	float NdotH = clamp(dot(normal, halfAngle), 0.0, 1.0);

	// Specular power lookup
	float specPower = texture(specularTable, vec2(NdotH, 0.5)).r;

	// Light projection (projective texturing)
	vec2 lightProjTC = inLightProjectTC.xy / inLightProjectTC.w;
	vec4 lightColor = texture(lightProjection, lightProjTC);

	// Light falloff attenuation
	float falloff = texture(lightFalloff, vec2(inLightFalloffTC.x, 0.5)).r;

	// Surface colors
	vec4 diffuseTex = texture(diffuseMap, inDiffuseTC);
	vec4 specularTex = texture(specularMap, inSpecularTC);

	// Combine
	vec3 diffuse = NdotL * diffuseColor.rgb * diffuseTex.rgb;
	vec3 specular = specPower * specularColor.rgb * specularTex.rgb;

	vec3 result = (diffuse + specular) * lightColor.rgb * falloff;
	result *= inVertexColor.rgb;

	// Gamma correction
	if (gammaBrightness.w > 0.0) {
		result *= gammaBrightness.rgb;
		result = pow(result, vec3(gammaBrightness.w));
	}

	outColor = vec4(result, 1.0);
}
