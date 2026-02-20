#version 460
#extension GL_EXT_ray_tracing : require

// Closest hit shader: record world-space surface position and approximate
// normal from the incoming ray direction.
//
// Note: accurate interpolated vertex normals require GL_EXT_buffer_reference
// to access per-vertex data via SBT hit group records. For this first pass
// we approximate using the negated ray direction as the face normal.
struct HitPayload {
    vec3 worldPos;
    vec3 worldNormal;
    bool hit;
};

layout(location=0) rayPayloadInEXT HitPayload payload;

// baryCoords.x = β (weight of v1), baryCoords.y = γ (weight of v2)
hitAttributeEXT vec2 baryCoords;

void main() {
    payload.worldPos    = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    // Approximate face normal: opposite of the incoming ray direction.
    // This gives diffuse shading but ignores the actual surface normal.
    payload.worldNormal = normalize(-gl_WorldRayDirectionEXT);
    payload.hit         = true;
}
