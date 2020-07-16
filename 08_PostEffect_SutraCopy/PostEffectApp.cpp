#include "PostEffectApp.h"
#include "VulkanBookUtil.h"
#include "TeapotModel.h"

#include <random>
#include <array>

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

#include <glm/gtc/matrix_transform.hpp>

static glm::vec4 colorSet[] = {
	glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
	glm::vec4(1.0f, 0.65f, 1.0f, 1.0f),
	glm::vec4(0.1f, 0.5f, 1.0f, 1.0f),
	glm::vec4(0.6f, 1.0f, 0.8f, 1.0f),
};

void PostEffectApp::Prepare()
{
	CreateRenderPass();

	// デプスバッファを準備する
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
	m_depthBuffer = CreateImage(extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

	// フレームバッファを準備
	PrepareFramebuffers();

	uint32_t imageCount = m_swapchain->GetImageCount();

	m_commandFences.resize(imageCount);
	VkFenceCreateInfo fenceCI{};
	fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCI.pNext = nullptr;
	fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (uint32_t i = 0; i < imageCount; i++)
	{
		VkResult result = vkCreateFence(m_device, &fenceCI, nullptr, &m_commandFences[i]);
		ThrowIfFailed(result, "vkCreateFence Failed.");
	}

	m_commandBuffers.resize(imageCount);
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.pNext = nullptr;
	allocInfo.commandPool = m_commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = imageCount;

	VkResult result = vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data());
	ThrowIfFailed(result, "vkAllocateCommandBuffers Failed.");

	PrepareRenderTexture();

	PrepareInstanceData();
	PrepareTeapot();
	PreparePlane();

	CreatePipelineTeapot();
	CreatePipelinePlane();

	PrepareDescriptors();
	PreparePostEffectDescriptors();

	// ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui_ImplGlfw_InitForVulkan(m_window, true);

	ImGui_ImplVulkan_InitInfo info{};
	info.Instance = m_vkInstance;
	info.PhysicalDevice = m_physicalDevice;
	info.Device = m_device;
	info.QueueFamily = m_gfxQueueIndex;
	info.Queue = m_deviceQueue;
	info.DescriptorPool = m_descriptorPool;
	info.MinImageCount = imageCount;
	info.ImageCount = imageCount;

	VkRenderPass renderPass = GetRenderPass("main");
	ImGui_ImplVulkan_Init(&info, renderPass);

	const VkCommandBuffer& command = CreateCommandBuffer();
	ImGui_ImplVulkan_CreateFontsTexture(command);
	FinishCommandBuffer(command);
	vkEndCommandBuffer(m_commandBuffers[0]);
}

void PostEffectApp::Cleanup()
{
	DestroyModelData(m_teapot);
	DestroyModelData(m_plane);

	for (const LayoutInfo& layout : { m_layoutTeapot, m_layoutPlane })
	{
		vkDestroyDescriptorSetLayout(m_device, layout.descriptorSet, nullptr);
		vkDestroyPipelineLayout(m_device, layout.pipeline, nullptr);
	}

	DestroyImage(m_depthBuffer);
	uint32_t count = uint32_t(m_framebuffers.size());
	DestroyFramebuffers(count, m_framebuffers.data());
	m_framebuffers.clear();

	DestroyImage(m_colorTarget);
	DestroyImage(m_depthTarget);

	for (const VkFence& f : m_commandFences)
	{
		vkDestroyFence(m_device, f, nullptr);
	}
	m_commandFences.clear();

	vkFreeCommandBuffers(m_device, m_commandPool, uint32_t(m_commandBuffers.size()), m_commandBuffers.data());
	m_commandBuffers.clear();

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

void PostEffectApp::Render()
{
	if (m_isMinimizedWindow)
	{
		MsgLoopMinimizedWindow();
	}

	uint32_t imageIndex = 0;
	VkResult result = m_swapchain->AcquireNextImage(&imageIndex, m_presentCompletedSem);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		return;
	}

	m_frameIndex = imageIndex;
	VkCommandBuffer& command = m_commandBuffers[m_frameIndex];
	const VkFence& fence = m_commandFences[m_frameIndex];
	result = vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
	ThrowIfFailed(result, "vkWaitForFences Failed.");
	result = vkResetFences(m_device, 1, &fence);
	ThrowIfFailed(result, "vkResetFences Failed.");

	VkCommandBufferBeginInfo commandBI{};
	commandBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	commandBI.pNext = nullptr;
	commandBI.flags = 0;
	commandBI.pInheritanceInfo = nullptr;
	result = vkBeginCommandBuffer(command, &commandBI);
	ThrowIfFailed(result, "vkBeginCommandBuffer Failed.");

	RenderToTexture(command);

	VkImageMemoryBarrier imageBarrier{};
	imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageBarrier.pNext = nullptr;
	imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrier.image = m_colorTarget.image;
	imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageBarrier.subresourceRange.baseMipLevel = 0;
	imageBarrier.subresourceRange.levelCount = 1;
	imageBarrier.subresourceRange.baseArrayLayer = 0;
	imageBarrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_DEPENDENCY_BY_REGION_BIT,
		0, nullptr, // memoryBarrier
		0, nullptr, // bufferMemoryBarrier
		1, &imageBarrier // imageMemoryBarrier
	);

	RenderToMain(command);

	// コマンドバッファ実行
	VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = nullptr;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &m_presentCompletedSem;
	submitInfo.pWaitDstStageMask = &waitStageMask;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &command;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &m_renderCompletedSem;

	result = vkEndCommandBuffer(command);
	ThrowIfFailed(result, "vkEndCommandBuffer Failed.");
	result = vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);
	ThrowIfFailed(result, "vkQueueSubmit Failed.");

	m_swapchain->QueuePresent(m_deviceQueue, imageIndex, m_renderCompletedSem);

	m_frameCount++;
}

void PostEffectApp::CreateRenderPass()
{
	// 2個のレンダーパスで同じようなコードを書くが、本でソース共通化してないので共通化しない
	const VkRenderPass& renderPassMain = book_util::CreateRenderPass(
		m_device,
		m_swapchain->GetSurfaceFormat().format,
		VK_FORMAT_D32_SFLOAT
	);
	RegisterRenderPass("main", renderPassMain);

	const VkRenderPass& renderPassRenderTarget = book_util::CreateRenderPassToRenderTarget(
		m_device,
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_D32_SFLOAT
	);
	RegisterRenderPass("render_target", renderPassRenderTarget);
}

void PostEffectApp::PrepareFramebuffers()
{
	uint32_t imageCount = m_swapchain->GetImageCount();
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();

	m_framebuffers.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; i++)
	{
		std::vector<VkImageView> views;
		views.push_back(m_swapchain->GetImageView(i));
		views.push_back(m_depthBuffer.view);

		VkRenderPass renderPass = GetRenderPass("main");
		m_framebuffers[i] = CreateFramebuffer(renderPass, extent.width, extent.height, uint32_t(views.size()), views.data());
	}
}

bool PostEffectApp::OnSizeChanged(uint32_t width, uint32_t height)
{
	bool result = VulkanAppBase::OnSizeChanged(width, height);
	if (result)
	{
		DestroyImage(m_depthBuffer);
		DestroyFramebuffers(uint32_t(m_framebuffers.size()), m_framebuffers.data());

		// デプスバッファを再生成
		const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
		m_depthBuffer = CreateImage(extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

		// フレームバッファを準備
		PrepareFramebuffers();

		// ポストエフェクト用リソースの削除＆再生成
		DestroyImage(m_colorTarget);
		DestroyImage(m_depthTarget);
		DestroyFramebuffers(1, &m_renderTextureFB);

		PrepareRenderTexture();

		// ディスクリプタを更新
		PreparePostEffectDescriptors();
	}

	return result;
}

void PostEffectApp::PrepareTeapot()
{
	// ステージ用のVBとIB、ターゲットのVBとIBの用意
	uint32_t bufferSizeVB = uint32_t(sizeof(TeapotModel::TeapotVerticesPN));
	VkBufferUsageFlags usageVB = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkMemoryPropertyFlags srcMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	VkMemoryPropertyFlags dstMemoryProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	const BufferObject& stageVB = CreateBuffer(bufferSizeVB, usageVB | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, srcMemoryProps);
	const BufferObject& targetVB = CreateBuffer(bufferSizeVB, usageVB | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dstMemoryProps);

	uint32_t bufferSizeIB = uint32_t(sizeof(TeapotModel::TeapotIndices));
	VkBufferUsageFlags usageIB = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	const BufferObject& stageIB = CreateBuffer(bufferSizeIB, usageIB | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, srcMemoryProps);
	const BufferObject& targetIB = CreateBuffer(bufferSizeIB, usageIB | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dstMemoryProps);

	// ステージ用のVBとIBにデータをコピー
	void* p = nullptr;
	vkMapMemory(m_device, stageVB.memory, 0, VK_WHOLE_SIZE, 0, &p);
	memcpy(p, TeapotModel::TeapotVerticesPN, bufferSizeVB);
	vkUnmapMemory(m_device, stageVB.memory);
	vkMapMemory(m_device, stageIB.memory, 0, VK_WHOLE_SIZE, 0, &p);
	memcpy(p, TeapotModel::TeapotIndices, bufferSizeIB);
	vkUnmapMemory(m_device, stageIB.memory);

	// ターゲットのVBとIBにデータをコピーするコマンドの実行
	VkCommandBuffer command = CreateCommandBuffer();
	VkBufferCopy copyRegionVB{}, copyRegionIB{};
	copyRegionVB.size = bufferSizeVB;
	copyRegionIB.size = bufferSizeIB;
	vkCmdCopyBuffer(command, stageVB.buffer, targetVB.buffer, 1, &copyRegionVB);
	vkCmdCopyBuffer(command, stageIB.buffer, targetIB.buffer, 1, &copyRegionIB);
	FinishCommandBuffer(command);
	vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);

	m_teapot.vertexBuffer = targetVB;
	m_teapot.indexBuffer = targetIB;
	m_teapot.indexCount = _countof(TeapotModel::TeapotIndices);
	m_teapot.vertexCount = _countof(TeapotModel::TeapotVerticesPN);

	DestroyBuffer(stageVB);
	DestroyBuffer(stageIB);

	// 定数バッファの準備
	uint32_t imageCount = m_swapchain->GetImageCount();
	VkMemoryPropertyFlags uboMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	uint32_t bufferSize = uint32_t(sizeof(ShaderParameters));
	m_teapot.sceneUB = CreateUniformBuffers(bufferSize, imageCount);

	// teapot用のディスクリプタセット/レイアウトを準備
	LayoutInfo layout{};
	VkDescriptorSetLayoutBinding descSetLayoutBindings[2];
	descSetLayoutBindings[0].binding = 0;
	descSetLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descSetLayoutBindings[0].descriptorCount = 1;
	descSetLayoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	descSetLayoutBindings[0].pImmutableSamplers = nullptr;
	descSetLayoutBindings[1].binding = 1;
	descSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descSetLayoutBindings[1].descriptorCount = 1;
	descSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	descSetLayoutBindings[1].pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo descSetLayoutCI{};
	descSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCI.pNext = nullptr;
	descSetLayoutCI.flags = 0;
	descSetLayoutCI.bindingCount = _countof(descSetLayoutBindings);
	descSetLayoutCI.pBindings = descSetLayoutBindings;
	VkResult result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &layout.descriptorSet);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");

	// パイプラインレイアウトを準備
	VkPipelineLayoutCreateInfo pipelineLayoutCI{};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.pNext = nullptr;
	pipelineLayoutCI.flags = 0;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &layout.descriptorSet;
	pipelineLayoutCI.pushConstantRangeCount = 0;
	pipelineLayoutCI.pPushConstantRanges = nullptr;
	result = vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &layout.pipeline);
	ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");

	m_layoutTeapot = layout;
}

void PostEffectApp::PrepareInstanceData()
{
	VkMemoryPropertyFlags memoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	// インスタンシング用のユニフォームバッファを準備
	uint32_t bufferSize = uint32_t(sizeof(InstanceData)) * InstanceCount;
	m_instanceUniforms.resize(m_swapchain->GetImageCount());
	for (BufferObject& ubo : m_instanceUniforms)
	{
		ubo = CreateBuffer(bufferSize, usage, memoryProps);
	}

	std::random_device rnd;
	std::vector<InstanceData> data(InstanceCount);

	for (uint32_t i = 0; i < InstanceCount; ++i)
	{
		const glm::vec3& axisX = glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3& axisZ = glm::vec3(0.0f, 0.0f, 1.0f);
		float k = float(rnd() % 360);
		float x = (i % 6) * 3.0f;
		float z = (i / 6) * -3.0f;

		glm::mat4 mat(1.0f);
		mat = glm::translate(mat, glm::vec3(x, 0.0f, z));
		mat = glm::rotate(mat, k, axisX);
		mat = glm::rotate(mat, k, axisZ);

		data[i].world = mat;
		data[i].color = colorSet[i % _countof(colorSet)];
	}

	for (const BufferObject& ubo : m_instanceUniforms)
	{
		void* p = nullptr;
		vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
		memcpy(p, data.data(), bufferSize);
		vkUnmapMemory(m_device, ubo.memory);
	}
}

void PostEffectApp::PrepareDescriptors()
{
	// teapot用ディスクリプタ準備
	VkDescriptorSetAllocateInfo descriptorSetAI{};
	descriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAI.pNext = nullptr;
	descriptorSetAI.descriptorPool = m_descriptorPool;
	descriptorSetAI.descriptorSetCount = 1;
	descriptorSetAI.pSetLayouts = &m_layoutTeapot.descriptorSet;

	uint32_t imageCount = m_swapchain->GetImageCount();
	m_teapot.descriptorSet.reserve(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		VkResult result = vkAllocateDescriptorSets(m_device, &descriptorSetAI, &descriptorSet);
		ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");

		m_teapot.descriptorSet.push_back(descriptorSet);
	}

	// ディスクリプタに書き込む
	for (size_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorBufferInfo uniformBufferInfo{};
		uniformBufferInfo.buffer = m_teapot.sceneUB[i].buffer;
		uniformBufferInfo.offset = 0;
		uniformBufferInfo.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo instanceBufferInfo{};
		instanceBufferInfo.buffer = m_instanceUniforms[i].buffer;
		instanceBufferInfo.offset = 0;
		instanceBufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet descSetSceneUB = book_util::PrepareWriteDescriptorSet(
			m_teapot.descriptorSet[i],
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
		);
		descSetSceneUB.pBufferInfo = &uniformBufferInfo;

		VkWriteDescriptorSet descSetInstUB = book_util::PrepareWriteDescriptorSet(
			m_teapot.descriptorSet[i],
			1,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
		);
		descSetInstUB.pBufferInfo = &instanceBufferInfo;

		std::array<VkWriteDescriptorSet, 2> writeDescriptorSets{
			descSetSceneUB, descSetInstUB
		};

		uint32_t count = uint32_t(writeDescriptorSets.size());
		vkUpdateDescriptorSets(m_device, count, writeDescriptorSets.data(), 0, nullptr);
	}
}

void PostEffectApp::PreparePostEffectDescriptors()
{
	// ディスクリプタに書き込む
	uint32_t imageCount = m_swapchain->GetImageCount();

	for (size_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorBufferInfo effectUbo{};
		effectUbo.buffer = m_plane.sceneUB[i].buffer;
		effectUbo.offset = 0;
		effectUbo.range = VK_WHOLE_SIZE;
		VkWriteDescriptorSet descSetSceneUB = book_util::PrepareWriteDescriptorSet(
			m_plane.descriptorSet[i],
			0, // dstBinding。VkDescriptorSetLayoutBindingのbinding、シェーダでのbinding値と一致する必要がある
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
		);
		descSetSceneUB.pBufferInfo = &effectUbo;
		// vkUpdateDescriptorSetsは複数のVkDescriptorBufferInfoをまとめてもやってもいいしこのように一個ずつやってもよい
		vkUpdateDescriptorSets(m_device, 1, &descSetSceneUB, 0, nullptr);

		VkDescriptorImageInfo texInfo{};
		texInfo.sampler = m_sampler;
		texInfo.imageView = m_colorTarget.view;
		texInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		VkWriteDescriptorSet descSetTexture = book_util::PrepareWriteDescriptorSet(
			m_plane.descriptorSet[i],
			1, // dstBinding。VkDescriptorSetLayoutBindingのbinding、シェーダでのbinding値と一致する必要がある
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
		);
		descSetTexture.pImageInfo = &texInfo;
		vkUpdateDescriptorSets(m_device, 1, &descSetTexture, 0, nullptr);
	}
}

void PostEffectApp::PreparePlane()
{
	// テクスチャを貼る板用のディスクリプタセット/レイアウトを準備
	LayoutInfo layout{};
	VkDescriptorSetLayoutBinding descSetLayoutBindings[2];
	descSetLayoutBindings[0].binding = 0;
	descSetLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descSetLayoutBindings[0].descriptorCount = 1;
	descSetLayoutBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	descSetLayoutBindings[0].pImmutableSamplers = nullptr;
	descSetLayoutBindings[1].binding = 1;
	descSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descSetLayoutBindings[1].descriptorCount = 1;
	descSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	descSetLayoutBindings[1].pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo descSetLayoutCI{};
	descSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCI.pNext = nullptr;
	descSetLayoutCI.flags = 0;
	descSetLayoutCI.bindingCount = _countof(descSetLayoutBindings);
	descSetLayoutCI.pBindings = descSetLayoutBindings;
	VkResult result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &layout.descriptorSet);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");

	VkDescriptorSetAllocateInfo descriptorSetAI{};
	descriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAI.pNext = nullptr;
	descriptorSetAI.descriptorPool = m_descriptorPool;
	descriptorSetAI.descriptorSetCount = 1;
	descriptorSetAI.pSetLayouts = &layout.descriptorSet;

	uint32_t imageCount = m_swapchain->GetImageCount();
	m_plane.descriptorSet.reserve(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		result = vkAllocateDescriptorSets(m_device, &descriptorSetAI, &descriptorSet);
		ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");

		m_plane.descriptorSet.push_back(descriptorSet);
	}

	VkSamplerCreateInfo samplerCI{};
	samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCI.pNext = nullptr;
	samplerCI.flags = 0;
	samplerCI.magFilter = VK_FILTER_LINEAR;
	samplerCI.minFilter = VK_FILTER_LINEAR;
	samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.mipLodBias = 0.0f;
	samplerCI.anisotropyEnable = VK_FALSE;
	samplerCI.maxAnisotropy = 1.0f;
	samplerCI.compareEnable = VK_FALSE;
	samplerCI.compareOp = VK_COMPARE_OP_NEVER;
	samplerCI.minLod = 0.0f;
	samplerCI.maxLod = 0.0f;
	samplerCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
	samplerCI.unnormalizedCoordinates = VK_FALSE;
	result = vkCreateSampler(m_device, &samplerCI, nullptr, &m_sampler);
	ThrowIfFailed(result, "vkCreateSampler Failed.");

	// エフェクト用定数バッファ確保
	uint32_t bufferSize = uint32_t(sizeof(EffectParameters));
	m_plane.sceneUB = CreateUniformBuffers(bufferSize, imageCount);

	// パイプラインレイアウトを準備
	VkPipelineLayoutCreateInfo pipelineLayoutCI{};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.pNext = nullptr;
	pipelineLayoutCI.flags = 0;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &layout.descriptorSet;
	pipelineLayoutCI.pushConstantRangeCount = 0;
	pipelineLayoutCI.pPushConstantRanges = nullptr;
	result = vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &layout.pipeline);
	ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");

	m_layoutPlane = layout;
}

void PostEffectApp::CreatePipelineTeapot()
{
	// Teapot用パイプライン
	uint32_t stride = uint32_t(sizeof(TeapotModel::Vertex));

	VkVertexInputBindingDescription vibDesc{};
	vibDesc.binding = 0;
	vibDesc.stride = stride;
	vibDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	std::array<VkVertexInputAttributeDescription, 2> inputAttribs{};
	inputAttribs[0].location = 0;
	inputAttribs[0].binding = 0;
	inputAttribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	inputAttribs[0].offset = offsetof(TeapotModel::Vertex, Position);
	inputAttribs[1].location = 1;
	inputAttribs[1].binding = 0;
	inputAttribs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	inputAttribs[1].offset = offsetof(TeapotModel::Vertex, Normal);

	VkPipelineVertexInputStateCreateInfo pipelineVisCI{};
	pipelineVisCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	pipelineVisCI.pNext = nullptr;
	pipelineVisCI.flags = 0;
	pipelineVisCI.vertexBindingDescriptionCount = 1;
	pipelineVisCI.pVertexBindingDescriptions = &vibDesc;
	pipelineVisCI.vertexAttributeDescriptionCount = uint32_t(inputAttribs.size());
	pipelineVisCI.pVertexAttributeDescriptions = inputAttribs.data();

	const VkPipelineColorBlendAttachmentState& colorBlendAttachmentState = book_util::GetOpaqueColorBlendAttachmentState();

	VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
	colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCI.pNext = nullptr;
	colorBlendStateCI.flags = 0;
	colorBlendStateCI.logicOpEnable = VK_FALSE;
	colorBlendStateCI.logicOp = VK_LOGIC_OP_CLEAR;
	colorBlendStateCI.attachmentCount = 1;
	colorBlendStateCI.pAttachments = &colorBlendAttachmentState;
	colorBlendStateCI.blendConstants[0] = 0.0f;
	colorBlendStateCI.blendConstants[1] = 0.0f;
	colorBlendStateCI.blendConstants[2] = 0.0f;
	colorBlendStateCI.blendConstants[3] = 0.0f;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI{};
	inputAssemblyCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyCI.pNext = nullptr;
	inputAssemblyCI.flags = 0;
	inputAssemblyCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssemblyCI.primitiveRestartEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampleCI{};
	multisampleCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleCI.pNext = nullptr;
	multisampleCI.flags = 0;
	multisampleCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampleCI.sampleShadingEnable = VK_FALSE;
	multisampleCI.minSampleShading = 0.0f;
	multisampleCI.pSampleMask = nullptr;
	multisampleCI.alphaToCoverageEnable = VK_FALSE;
	multisampleCI.alphaToOneEnable = VK_FALSE;
	
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();

	VkViewport viewport{};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = float(extent.width);
	viewport.height = float(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = 0; // VkPipelineDynamicStateCreateInfoを設定するときはVkViewportとVkScissorの設定はダミーになるので適当で大丈夫
	scissor.extent.height = 0; // VkPipelineDynamicStateCreateInfoを設定するときはVkViewportとVkScissorの設定はダミーになるので適当で大丈夫

	VkPipelineViewportStateCreateInfo viewportCI{};
	viewportCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportCI.pNext = nullptr;
	viewportCI.flags = 0;
	viewportCI.viewportCount = 1;
	viewportCI.pViewports = &viewport;
	viewportCI.scissorCount = 1;
	viewportCI.pScissors = &scissor;

	// シェーダのロード
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages
	{
		book_util::LoadShader(m_device, "modelVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
		book_util::LoadShader(m_device, "modelFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	std::vector<VkDynamicState> dynamicStates{
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_VIEWPORT
	};
	VkPipelineDynamicStateCreateInfo pipelineDynamicStateCI{};
	pipelineDynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	pipelineDynamicStateCI.pNext = nullptr;
	pipelineDynamicStateCI.flags = 0;
	pipelineDynamicStateCI.dynamicStateCount = uint32_t(dynamicStates.size());
	pipelineDynamicStateCI.pDynamicStates = dynamicStates.data();

	const VkPipelineRasterizationStateCreateInfo& rasterizerState = book_util::GetDefaultRasterizerState();

	const VkPipelineDepthStencilStateCreateInfo& dsState = book_util::GetDefaultDepthStencilState();

	// パイプライン構築
	VkRenderPass renderPass = GetRenderPass("render_target");
	VkGraphicsPipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCI.pNext = nullptr;
	pipelineCI.stageCount = uint32_t(shaderStages.size());
	pipelineCI.pStages = shaderStages.data();
	pipelineCI.pVertexInputState = &pipelineVisCI;
	pipelineCI.pInputAssemblyState = &inputAssemblyCI;
	pipelineCI.pTessellationState = nullptr;
	pipelineCI.pViewportState = &viewportCI;
	pipelineCI.pRasterizationState = &rasterizerState;
	pipelineCI.pMultisampleState = &multisampleCI;
	pipelineCI.pDepthStencilState = &dsState;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pDynamicState = &pipelineDynamicStateCI;
	pipelineCI.layout = m_layoutTeapot.pipeline;
	pipelineCI.renderPass = renderPass;
	pipelineCI.subpass = 0;
	pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCI.basePipelineIndex = 0;
	VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_teapot.pipeline);
	ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

	book_util::DestroyShaderModules(m_device, shaderStages);
}

void PostEffectApp::CreatePipelinePlane()
{
	// Plane用パイプライン
	// gl_VertexIndexでシェーダ内でPlaneのモデルを作ってるので
	// 頂点数だけあればいらないはずだがまだVertexPTを渡している
	uint32_t stride = uint32_t(sizeof(VertexPT));

	VkVertexInputBindingDescription vibDesc{};
	vibDesc.binding = 0;
	vibDesc.stride = stride;
	vibDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	std::array<VkVertexInputAttributeDescription, 2> inputAttribs{};
	inputAttribs[0].location = 0;
	inputAttribs[0].binding = 0;
	inputAttribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	inputAttribs[0].offset = offsetof(VertexPT, position);
	inputAttribs[1].location = 1;
	inputAttribs[1].binding = 0;
	inputAttribs[1].format = VK_FORMAT_R32G32_SFLOAT;
	inputAttribs[1].offset = offsetof(VertexPT, uv);

	VkPipelineVertexInputStateCreateInfo pipelineVisCI{};
	pipelineVisCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	pipelineVisCI.pNext = nullptr;
	pipelineVisCI.flags = 0;
	pipelineVisCI.vertexBindingDescriptionCount = 1;
	pipelineVisCI.pVertexBindingDescriptions = &vibDesc;
	pipelineVisCI.vertexAttributeDescriptionCount = uint32_t(inputAttribs.size());
	pipelineVisCI.pVertexAttributeDescriptions = inputAttribs.data();

	const VkPipelineColorBlendAttachmentState& colorBlendAttachmentState = book_util::GetOpaqueColorBlendAttachmentState();

	VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
	colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCI.pNext = nullptr;
	colorBlendStateCI.flags = 0;
	colorBlendStateCI.logicOpEnable = VK_FALSE;
	colorBlendStateCI.logicOp = VK_LOGIC_OP_CLEAR;
	colorBlendStateCI.attachmentCount = 1;
	colorBlendStateCI.pAttachments = &colorBlendAttachmentState;
	colorBlendStateCI.blendConstants[0] = 0.0f;
	colorBlendStateCI.blendConstants[1] = 0.0f;
	colorBlendStateCI.blendConstants[2] = 0.0f;
	colorBlendStateCI.blendConstants[3] = 0.0f;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI{};
	inputAssemblyCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyCI.pNext = nullptr;
	inputAssemblyCI.flags = 0;
	inputAssemblyCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; // こちらはSTRIP インデックスバッファが0,1,2,3
	inputAssemblyCI.primitiveRestartEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampleCI{};
	multisampleCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleCI.pNext = nullptr;
	multisampleCI.flags = 0;
	multisampleCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampleCI.sampleShadingEnable = VK_FALSE;
	multisampleCI.minSampleShading = 0.0f;
	multisampleCI.pSampleMask = nullptr;
	multisampleCI.alphaToCoverageEnable = VK_FALSE;
	multisampleCI.alphaToOneEnable = VK_FALSE;
	
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();

	const VkViewport& viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));

	VkRect2D scissor{};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = 0; // VkPipelineDynamicStateCreateInfoを設定するときはVkViewportとVkScissorの設定はダミーになるので適当で大丈夫
	scissor.extent.height = 0; // VkPipelineDynamicStateCreateInfoを設定するときはVkViewportとVkScissorの設定はダミーになるので適当で大丈夫

	VkPipelineViewportStateCreateInfo viewportCI{};
	viewportCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportCI.pNext = nullptr;
	viewportCI.flags = 0;
	viewportCI.viewportCount = 1;
	viewportCI.pViewports = &viewport;
	viewportCI.scissorCount = 1;
	viewportCI.pScissors = &scissor;

	// シェーダのロード
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages
	{
		book_util::LoadShader(m_device, "quadVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
		book_util::LoadShader(m_device, "planeFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	std::vector<VkDynamicState> dynamicStates{
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_VIEWPORT
	};
	VkPipelineDynamicStateCreateInfo pipelineDynamicStateCI{};
	pipelineDynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	pipelineDynamicStateCI.pNext = nullptr;
	pipelineDynamicStateCI.flags = 0;
	pipelineDynamicStateCI.dynamicStateCount = uint32_t(dynamicStates.size());
	pipelineDynamicStateCI.pDynamicStates = dynamicStates.data();

	const VkPipelineRasterizationStateCreateInfo& rasterizerState = book_util::GetDefaultRasterizerState();

	const VkPipelineDepthStencilStateCreateInfo& dsState = book_util::GetDefaultDepthStencilState();

	// パイプライン構築
	VkRenderPass renderPass = GetRenderPass("main");
	VkGraphicsPipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCI.pNext = nullptr;
	pipelineCI.stageCount = uint32_t(shaderStages.size());
	pipelineCI.pStages = shaderStages.data();
	pipelineCI.pVertexInputState = &pipelineVisCI;
	pipelineCI.pInputAssemblyState = &inputAssemblyCI;
	pipelineCI.pTessellationState = nullptr;
	pipelineCI.pViewportState = &viewportCI;
	pipelineCI.pRasterizationState = &rasterizerState;
	pipelineCI.pMultisampleState = &multisampleCI;
	pipelineCI.pDepthStencilState = &dsState;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pDynamicState = &pipelineDynamicStateCI;
	pipelineCI.layout = m_layoutPlane.pipeline;
	pipelineCI.renderPass = renderPass;
	pipelineCI.subpass = 0;
	pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCI.basePipelineIndex = 0;
	VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_plane.pipeline);
	ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

	book_util::DestroyShaderModules(m_device, shaderStages);
}

void PostEffectApp::PrepareRenderTexture()
{
	// 描画先テクスチャの準備
	ImageObject colorTarget;
	VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
	VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
	const VkExtent2D& surfaceExtent = m_swapchain->GetSurfaceExtent();
	uint32_t width = surfaceExtent.width;
	uint32_t height = surfaceExtent.height;

	{
		VkImageCreateInfo imageCI{};
		imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCI.pNext = nullptr;
		imageCI.flags = 0;
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = colorFormat;
		imageCI.extent.width = width;
		imageCI.extent.height = height;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCI.queueFamilyIndexCount = 0;
		imageCI.pQueueFamilyIndices = nullptr;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkResult result = vkCreateImage(m_device, &imageCI, nullptr, &colorTarget.image);
		ThrowIfFailed(result, "vkCreateImage Failed.");

		// メモリ量の算出
		VkMemoryRequirements reqs;
		vkGetImageMemoryRequirements(m_device, colorTarget.image, &reqs);

		VkMemoryAllocateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		info.pNext = nullptr;
		info.allocationSize = reqs.size;
		info.memoryTypeIndex = GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		result = vkAllocateMemory(m_device, &info, nullptr, &colorTarget.memory);
		ThrowIfFailed(result, "vkAllocateMemory Failed.");
		result = vkBindImageMemory(m_device, colorTarget.image, colorTarget.memory, 0);
		ThrowIfFailed(result, "vkBindImageMemory Failed.");

		VkImageViewCreateInfo viewCI{};
		viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCI.pNext = nullptr;
		viewCI.flags = 0;
		viewCI.image = colorTarget.image;
		viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCI.format = imageCI.format;
		viewCI.components = book_util::DefaultComponentMapping();
		viewCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		result = vkCreateImageView(m_device, &viewCI, nullptr, &colorTarget.view);
		ThrowIfFailed(result, "vkCreateImageView Failed.");
	}

	// 描画先デプスバッファの準備
	ImageObject depthTarget;
	{
		VkImageCreateInfo imageCI{};
		imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCI.pNext = nullptr;
		imageCI.flags = 0;
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = depthFormat;
		imageCI.extent.width = width;
		imageCI.extent.height = height;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCI.queueFamilyIndexCount = 0;
		imageCI.pQueueFamilyIndices = nullptr;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkResult result = vkCreateImage(m_device, &imageCI, nullptr, &depthTarget.image);
		ThrowIfFailed(result, "vkCreateImage Failed.");

		// メモリ量の算出
		VkMemoryRequirements reqs;
		vkGetImageMemoryRequirements(m_device, depthTarget.image, &reqs);

		VkMemoryAllocateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		info.pNext = nullptr;
		info.allocationSize = reqs.size;
		info.memoryTypeIndex = GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		result = vkAllocateMemory(m_device, &info, nullptr, &depthTarget.memory);
		ThrowIfFailed(result, "vkAllocateMemory Failed.");
		result = vkBindImageMemory(m_device, depthTarget.image, depthTarget.memory, 0);
		ThrowIfFailed(result, "vkBindImageMemory Failed.");

		VkImageViewCreateInfo viewCI{};
		viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCI.pNext = nullptr;
		viewCI.flags = 0;
		viewCI.image = depthTarget.image;
		viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCI.format = imageCI.format;
		viewCI.components = book_util::DefaultComponentMapping();
		viewCI.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
		result = vkCreateImageView(m_device, &viewCI, nullptr, &depthTarget.view);
		ThrowIfFailed(result, "vkCreateImageView Failed.");
	}

	m_colorTarget = colorTarget;
	m_depthTarget = depthTarget;

	std::vector<VkImageView> views;
	views.push_back(m_colorTarget.view);
	views.push_back(m_depthTarget.view);
	VkRenderPass renderPass = GetRenderPass("render_target");
	m_renderTextureFB = CreateFramebuffer(renderPass, width, height, uint32_t(views.size()), views.data());
}

void PostEffectApp::RenderToTexture(const VkCommandBuffer& command)
{
	std::array<VkClearValue, 2> clearValue = {
		{
			{0.2f, 0.65f, 0.0f, 0.0f}, // for Color
			{1.0f, 0}, // for Depth
		}
	};

	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
	VkRect2D renderArea{};
	renderArea.offset.x = 0;
	renderArea.offset.y = 0;
	renderArea.extent = extent;

	VkRenderPass renderPass = GetRenderPass("render_target");
	VkRenderPassBeginInfo rpBI{};
	rpBI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBI.pNext = nullptr;
	rpBI.renderPass = renderPass;
	rpBI.framebuffer = m_renderTextureFB;
	rpBI.renderArea = renderArea;
	rpBI.clearValueCount = uint32_t(clearValue.size());
	rpBI.pClearValues = clearValue.data();

	{
		ShaderParameters shaderParams{};
		shaderParams.view = glm::lookAtRH(
			glm::vec3(3.0f, 5.0f, 5.0f),
			glm::vec3(3.0f, 2.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f)
		);

		shaderParams.proj = glm::perspectiveRH(
			glm::radians(45.0f),
			float(extent.width) / float(extent.height),
			0.1f,
			1000.0f
		);

		const BufferObject& ubo = m_teapot.sceneUB[m_frameIndex];
		void* p = nullptr;
		VkResult result = vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
		ThrowIfFailed(result, "vkMapMemory Failed.");
		memcpy(p, &shaderParams, sizeof(ShaderParameters));
		vkUnmapMemory(m_device, ubo.memory);
	}

	VkViewport viewport;
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = float(extent.width);
	viewport.height = float(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent = extent;

	vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdSetScissor(command, 0, 1, &scissor);
	vkCmdSetViewport(command, 0, 1, &viewport);

	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_teapot.pipeline);
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layoutTeapot.pipeline, 0, 1, &m_teapot.descriptorSet[m_frameIndex], 0, nullptr);
	vkCmdBindIndexBuffer(command, m_teapot.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.vertexBuffer.buffer, offsets);
	vkCmdDrawIndexed(command, m_teapot.indexCount, InstanceCount, 0, 0, 0);

	vkCmdEndRenderPass(command);
}

void PostEffectApp::RenderToMain(const VkCommandBuffer& command)
{
	std::array<VkClearValue, 2> clearValue = {
		{
			{0.85f, 0.5f, 0.5f, 0.0f}, // for Color
			{1.0f, 0}, // for Depth
		}
	};

	VkExtent2D surfaceExtent = m_swapchain->GetSurfaceExtent();

	VkRect2D renderArea{};
	renderArea.offset.x = 0;
	renderArea.offset.y = 0;
	renderArea.extent = surfaceExtent;

	VkRenderPass renderPass = GetRenderPass("main");
	VkRenderPassBeginInfo rpBI{};
	rpBI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBI.pNext = nullptr;
	rpBI.renderPass = renderPass;
	rpBI.framebuffer = m_framebuffers[m_frameIndex];
	rpBI.renderArea = renderArea;
	rpBI.clearValueCount = uint32_t(clearValue.size());
	rpBI.pClearValues = clearValue.data();

	{
		m_effectParameter.frameCount = m_frameCount;
		const glm::vec2& screenSize = glm::vec2(
			float(surfaceExtent.width),
			float(surfaceExtent.height)
		);

		const BufferObject& ubo = m_plane.sceneUB[m_frameIndex];
		void* p = nullptr;
		vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
		memcpy(p, &m_effectParameter, sizeof(EffectParameters)); // 本ではなぜかsizeof(ShaderParameter)になっている
		vkUnmapMemory(m_device, ubo.memory);
	}

	vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_plane.pipeline);

	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
	const VkViewport& viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));

	VkOffset2D offset{};
	offset.x = 0;
	offset.y = 0;
	VkRect2D scissor{};
	scissor.offset = offset;
	scissor.extent = extent;

	vkCmdSetScissor(command, 0, 1, &scissor);
	vkCmdSetViewport(command, 0, 1, &viewport);

	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layoutPlane.pipeline, 0, 1, &m_plane.descriptorSet[m_frameIndex], 0, nullptr);
	vkCmdDraw(command, 4, 1, 0, 0);

	RenderImGui(command);

	vkCmdEndRenderPass(command);
}

void PostEffectApp::RenderImGui(const VkCommandBuffer& command)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	{
		ImGui::Begin("Control");
		ImGui::Text("DrawTeapot");
		float framerate = ImGui::GetIO().Framerate;
		ImGui::Text("Framerate(avg) %.3f ms/frame", 1000.0f / framerate);
		ImGui::End();
	}

	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

void PostEffectApp::DestroyModelData(ModelData& model)
{
	for (const BufferObject& bufObj : { model.vertexBuffer, model.indexBuffer })
	{
		DestroyBuffer(bufObj);
	}

	for (const BufferObject& ubo : model.sceneUB)
	{
		DestroyBuffer(ubo);
	}
	model.sceneUB.clear();

	vkDestroyPipeline(m_device, model.pipeline, nullptr);
	vkFreeDescriptorSets(m_device, m_descriptorPool, uint32_t(model.descriptorSet.size()), model.descriptorSet.data());
	model.descriptorSet.clear();
}

