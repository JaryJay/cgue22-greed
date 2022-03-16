#version 460

// VAO Buffer data
layout (location = 0) in vec3 vPosition; // vertex position
layout (location = 1) in vec3 vNormal; // vertex normal
layout (location = 2) in vec2 vUV; // vertex uv coordinate

layout(std140, binding = 0) uniform PerFrameData
{
	vec4 viewPos;
	mat4 ViewProj;
	mat4 ViewProjSkybox;
};

uniform mat4 model;

out vec3 fPosition;

void main()
{
	//gl_Position = ViewProjSkybox * model * vec4(vPosition, 1.0);
	//fPosition = vec3(model * vec4(vPosition, 1.0));

	fPosition = vPosition;

	vec4 clipPos = ViewProjSkybox * vec4(fPosition, 1.0);

	gl_Position = clipPos.xyww;
}