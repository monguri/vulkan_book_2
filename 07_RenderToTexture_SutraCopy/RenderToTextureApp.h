#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>

class RenderToTextureApp : public VulkanAppBase
{
public:
	virtual void Prepare() override;
	virtual void Cleanup() override;
	virtual void Render() override;

	virtual bool OnSizeChanged(uint32_t width, uint32_t height) override;

	enum
	{
		TextureWidth = 512,
		TextureHeight = 512,
	};
	enum
	{
		InstanceDataMax = 500
	};

private:
	ImageObject m_depthBuffer;
	std::vector<VkFramebuffer> m_framebuffers;
	std::vector<VkFence> m_commandFences;
	std::vector<VkCommandBuffer> m_commandBuffers;

	struct VertexPT
	{
		glm::vec3 position;
		glm::vec2 uv;
	};

	struct ModelData
	{
		BufferObject vertexBuffer;
		BufferObject indexBuffer;
		uint32_t vertexCount;
		uint32_t indexCount;

		std::vector<BufferObject> sceneUB;
		std::vector<VkDescriptorSet> descriptorSet;

		VkPipeline pipeline;
	};
	ModelData m_teapot;
	ModelData m_plane;

	struct LayoutInfo
	{
		VkDescriptorSetLayout descriptorSet = VK_NULL_HANDLE;
		VkPipelineLayout pipeline = VK_NULL_HANDLE;
	};
	LayoutInfo m_layoutTeapot;
	LayoutInfo m_layoutPlane;

	int m_instanceCount = 200;
	float m_cameraOffset = 0.0f;

	struct ShaderParameters
	{
		glm::mat4 view;
		glm::mat4 proj;
	};
	struct InstanceData
	{
		glm::mat4 world;
		glm::vec4 color;
	};

	std::vector<BufferObject> m_instanceUniforms;

	ImageObject m_colorTarget, m_depthTarget;
	VkFramebuffer m_renderTextureFB = VK_NULL_HANDLE;
	VkSampler m_sampler = VK_NULL_HANDLE;

	void CreateRenderPass();
	void PrepareFramebuffers();
	void PrepareRenderTexture();
	void PrepareTeapot();
	void PreparePlane();
	void PrepareInstanceData();
	void CreatePipeline();
	void DestroyModelData(ModelData& model);
};

