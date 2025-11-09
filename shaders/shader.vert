#version 460

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

void main(){
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
    
    
    vec3 lightPos = vec3(0.0, 0.0, 10.0);  // world-space position of point light
    vec3 lightColor = vec3(1.0, 0.95, 0.8);  // slightly warm light
    float lightIntensity = 50.0;  // brightness scale

    vec3 fragPos = inPosition;  // your fragment world position
    vec3 normal = normalize(inNormal);

    // Direction from fragment to light
    vec3 L = lightPos - fragPos;
    float distance = length(L);
    L = normalize(L);

    // Attenuation by distance^2
    float attenuation = lightIntensity / (distance * distance);

    // Diffuse (Lambert)
    float NdotL = max(dot(normal, L), 0.0);
    vec3 diffuse = attenuation * lightColor * NdotL;
    
    fragColor = inColor;
}