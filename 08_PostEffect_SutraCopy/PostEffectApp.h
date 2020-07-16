#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>

class PostEffectApp : public VulkanAppBase
{
public:
	virtual void Prepare() override;
	virtual void Cleanup() override;
	virtual void Render() override;

	virtual bool OnSizeChanged(uint32_t width, uint32_t height) override;

	enum
	{
		InstanceCount = 200
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
	std::vector<BufferObject> m_instanceUniforms;
	ModelData m_plane;

	struct LayoutInfo
	{
		VkDescriptorSetLayout descriptorSet = VK_NULL_HANDLE;
		VkPipelineLayout pipeline = VK_NULL_HANDLE;
	};
	LayoutInfo m_layoutTeapot;
	LayoutInfo m_layoutPlane;

	uint32_t m_frameIndex = 0;

	struct ShaderParameters
	{
		glm::mat4 view;
		glm::mat4 proj;
	};
	struct EffectParameters
	{
		glm::vec2 screenSize;
		float mosaicBlockSize = 10.0f;
		UINT frameCount = 0;
		float ripple = 0.75f;
		float speed = 1.5f;
		float distortion = 0.03f;
		float brightness = 0.25f;
	};
	EffectParameters m_effectParameter; // 使う場面のRenderでなくImguiから設定するのでメンバ変数にしておく必要がある

	struct InstanceData
	{
		glm::mat4 world;
		glm::vec4 color;
	};
	struct InstanceParameters
	{
		InstanceData data[InstanceCount];
	};

	ImageObject m_colorTarget, m_depthTarget;
	VkFramebuffer m_renderTextureFB = VK_NULL_HANDLE;
	VkSampler m_sampler = VK_NULL_HANDLE;

	uint32_t m_frameCount = 0;

	void CreateRenderPass();
	void PrepareFramebuffers();
	void PrepareRenderTexture();
	void PrepareTeapot();
	void PrepareInstanceData();
	void PreparePlane();
	void PrepareDescriptors();
	void PreparePostEffectDescriptors();
	void CreatePipelineTeapot();
	void CreatePipelinePlane();
	void RenderToTexture(const VkCommandBuffer& command);
	void RenderToMain(const VkCommandBuffer& command);
	void RenderImGui(const VkCommandBuffer& command);
	void DestroyModelData(ModelData& model);
};

