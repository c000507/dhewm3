#version 460
#extension GL_EXT_ray_tracing : require

// Primary ray miss: ray escaped the scene with no geometry hit.
struct HitPayload {
    vec3 worldPos;
    vec3 worldNormal;
    bool hit;
};

layout(location=0) rayPayloadInEXT HitPayload payload;

void main() {
    payload.hit = false;
}
