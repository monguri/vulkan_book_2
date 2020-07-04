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
	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> m_descriptorSets;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = nullptr;

	struct ModelData
	{
		BufferObject vertexBuffer;
		BufferObject indexBuffer;
		uint32_t vertexCount;
		uint32_t indexCount;
	};
	ModelData m_teapot;
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

	std::vector<BufferObject> m_uniformBuffers;
	std::vector<BufferObject> m_instanceUniforms;

	ImageObject m_colorTarget, m_depthTarget;
	VkFramebuffer m_renderTextureFB = VK_NULL_HANDLE;

	void CreateRenderPass();
	void PrepareFramebuffers();
	void PrepareRenderTexture();
	void PrepareTeapot();
	void PrepareInstanceData();
	void PrepareDescriptors();
	void CreatePipeline();
};

