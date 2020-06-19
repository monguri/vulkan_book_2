#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>

class DisplayHDR10App : public VulkanAppBase
{
public:
	virtual void Prepare() override;
	virtual void Cleanup() override;
	virtual void Render() override;

	virtual bool OnSizeChanged(uint32_t width, uint32_t height) override;

private:
	VkRenderPass m_renderPass;
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

	struct ShaderParameters
	{
		glm::mat4 world;
		glm::mat4 view;
		glm::mat4 proj;
		glm::vec4 lightPos;
		glm::vec4 cameraPos;
	};

	std::vector<BufferObject> m_uniformBuffers;

	void CreateRenderPass();
	void PrepareFramebuffers();
	void PrepareTeapot();
	void CreatePipeline();
};

