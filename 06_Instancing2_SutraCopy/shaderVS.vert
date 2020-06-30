#version 450

layout(location=0) in vec4 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec4 inOffsetPos;
layout(location=3) in vec4 inColor;

layout(location=0) out vec4 outColor;

out gl_PerVertex
{
	vec4 gl_Position;
};

layout(set=0, binding=0)
uniform SceneParameters
{
	mat4 world;
	mat4 view;
	mat4 proj;
};

void main()
{
	vec4 pos = inPos;
	pos.xyz += inOffsetPos.xyz;
	gl_Position = proj * view * world * pos;

	vec3 worldNormal = mat3(world) * inNormal;
	float l = dot(worldNormal, vec3(0.0f, 1.0f, 0.0f)) * 0.5f + 0.5f;
	vec3 color = vec3(l) * inColor.xyz;
	outColor = vec4(color, inColor.w);
}

