#version 460
#extension GL_EXT_ray_tracing : require

// Shadow ray miss: the ray reached the light without hitting any geometry,
// so the surface point is NOT in shadow.
layout(location=1) rayPayloadInEXT bool isShadowed;

void main() {
    isShadowed = false;
}
