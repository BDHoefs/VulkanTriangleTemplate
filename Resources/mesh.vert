#version 450

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;

layout (location = 0) out vec3 outColor;

layout(set = 0, binding = 0) uniform CameraBuffer {
	mat4 view;
	mat4 proj;
} cameraData;

layout(std140, set = 0, binding = 1) readonly buffer sceneBuffer {
	mat4 models[];
} sceneData;

layout(push_constant) uniform constants
{
	uint index;
} pushConstants;

void main()
{
	mat4 transformMatrix = (cameraData.proj * cameraData.view * sceneData.models[pushConstants.index]);
	gl_Position = transformMatrix * vec4(vPosition, 1.0f);
	outColor = vColor;
}
