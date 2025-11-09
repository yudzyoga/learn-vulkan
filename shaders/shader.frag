#version 460

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;
layout(binding = 1) uniform sampler2D texSampler;

void main(){
    outColor = vec4(fragColor * texture(texSampler, fragTexCoord).rgb, 1.0);
    // float val = pow(gl_FragCoord.z, 50.0);
    // outColor = vec4(val, val, val, 1.0);
}