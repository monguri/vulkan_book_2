#include "InstancingApp.h"
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

void InstancingApp::Prepare()
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

	PrepareTeapot();
	PrepareInstanceData();
	PrepareDescriptors();

	VkPipelineLayoutCreateInfo pipelineLayoutCI{};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.pNext = nullptr;
	pipelineLayoutCI.flags = 0;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &m_descriptorSetLayout;
	pipelineLayoutCI.pushConstantRangeCount = 0;
	pipelineLayoutCI.pPushConstantRanges = nullptr;
	result = vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &m_pipelineLayout);
	ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");

	CreatePipeline();

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
	ImGui_ImplVulkan_Init(&info, m_renderPass);

	VkCommandBufferBeginInfo beginInfo{
	VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	nullptr,
	VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(m_commandBuffers[0], &beginInfo);
	ImGui_ImplVulkan_CreateFontsTexture(m_commandBuffers[0]);
	vkEndCommandBuffer(m_commandBuffers[0]);
	VkSubmitInfo submitInfo{
	VK_STRUCTURE_TYPE_SUBMIT_INFO,nullptr,
	};
	submitInfo.pCommandBuffers = m_commandBuffers.data();
	submitInfo.commandBufferCount = 1;
	vkQueueSubmit(m_deviceQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkDeviceWaitIdle(m_device);
}

void InstancingApp::Cleanup()
{
	for (const BufferObject& ubo : m_uniformBuffers)
	{
		DestroyBuffer(ubo);
	}
	m_uniformBuffers.clear();

	for (const BufferObject& ubo : m_instanceUniforms)
	{
		DestroyBuffer(ubo);
	}
	m_instanceUniforms.clear();

	DestroyBuffer(m_teapot.vertexBuffer);
	DestroyBuffer(m_teapot.indexBuffer);

	vkFreeDescriptorSets(m_device, m_descriptorPool, uint32_t(m_descriptorSets.size()), m_descriptorSets.data());
	m_descriptorSets.clear();

	vkDestroyPipeline(m_device, m_pipeline, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
	vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

	vkDestroyRenderPass(m_device, m_renderPass, nullptr);

	DestroyImage(m_depthBuffer);
	uint32_t count = uint32_t(m_framebuffers.size());
	DestroyFramebuffers(count, m_framebuffers.data());
	m_framebuffers.clear();

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

void InstancingApp::Render()
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

	std::array<VkClearValue, 2> clearValue = {
		{
			{0.85f, 0.5f, 0.5f, 0.0f}, // for Color
			{1.0f, 0}, // for Depth
		}
	};

	VkRect2D renderArea{};
	renderArea.offset = VkOffset2D{ 0, 0 };
	renderArea.extent = m_swapchain->GetSurfaceExtent();

	VkRenderPassBeginInfo rpBI{};
	rpBI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBI.pNext = nullptr;
	rpBI.renderPass = m_renderPass;
	rpBI.framebuffer = m_framebuffers[imageIndex];
	rpBI.renderArea = renderArea;
	rpBI.clearValueCount = uint32_t(clearValue.size());
	rpBI.pClearValues = clearValue.data();

	VkCommandBufferBeginInfo commandBI{};
	commandBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	commandBI.pNext = nullptr;
	commandBI.flags = 0;
	commandBI.pInheritanceInfo = nullptr;

	{
		ShaderParameters shaderParam{};
		shaderParam.view = glm::lookAtRH(
			glm::vec3(3.0f, 5.0f, 10.0f - m_cameraOffset),
			glm::vec3(3.0f, 2.0f, 0.0f - m_cameraOffset),
			glm::vec3(0.0f, 1.0f, 0.0f)
		);

		const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
		shaderParam.proj = glm::perspectiveRH(
			glm::radians(45.0f),
			float(extent.width) / float(extent.height),
			0.1f,
			1000.0f
		);

		const BufferObject& ubo = m_uniformBuffers[imageIndex];
		void* p = nullptr;
		result = vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
		ThrowIfFailed(result, "vkMapMemory Failed.");
		memcpy(p, &shaderParam, sizeof(ShaderParameters));
		vkUnmapMemory(m_device, ubo.memory);
	}

	VkCommandBuffer& command = m_commandBuffers[imageIndex];
	const VkFence& fence = m_commandFences[imageIndex];
	result = vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
	ThrowIfFailed(result, "vkWaitForFences Failed.");

	result = vkBeginCommandBuffer(command, &commandBI);
	ThrowIfFailed(result, "vkBeginCommandBuffer Failed.");
	vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

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

	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[imageIndex], 0, nullptr);
	vkCmdBindIndexBuffer(command, m_teapot.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.vertexBuffer.buffer, offsets);
	vkCmdDrawIndexed(command, m_teapot.indexCount, m_instanceCount, 0, 0, 0);
	RenderImGui(command);

	vkCmdEndRenderPass(command);
	result = vkEndCommandBuffer(command);
	ThrowIfFailed(result, "vkEndCommandBuffer Failed.");

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

	result = vkResetFences(m_device, 1, &fence);
	ThrowIfFailed(result, "vkResetFences Failed.");
	result = vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);
	ThrowIfFailed(result, "vkQueueSubmit Failed.");

	m_swapchain->QueuePresent(m_deviceQueue, imageIndex, m_renderCompletedSem);
}

bool InstancingApp::OnSizeChanged(uint32_t width, uint32_t height)
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
	}

	return result;
}

void InstancingApp::RenderImGui(VkCommandBuffer command)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	{
		ImGui::Begin("Control");
		ImGui::Text("DrawTeapot");
		float framerate = ImGui::GetIO().Framerate;
		ImGui::Text("Framerate(avg) %.3f ms/frame", 1000.0f / framerate);
		ImGui::SliderInt("Count", &m_instanceCount, 1, InstanceDataMax);
		ImGui::SliderFloat("Camera", &m_cameraOffset, 0.0f, 50.0f);
		ImGui::End();
	}

	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

void InstancingApp::CreateRenderPass()
{
	std::array<VkAttachmentDescription, 2> attachments;
	VkAttachmentDescription& colorTarget = attachments[0];
	VkAttachmentDescription& depthTarget = attachments[1];

	colorTarget = VkAttachmentDescription{};
	colorTarget.flags = 0;
	colorTarget.format = m_swapchain->GetSurfaceFormat().format;
	colorTarget.samples = VK_SAMPLE_COUNT_1_BIT;
	colorTarget.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorTarget.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorTarget.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorTarget.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorTarget.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorTarget.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	depthTarget = VkAttachmentDescription{};
	depthTarget.flags = 0;
	depthTarget.format = VK_FORMAT_D32_SFLOAT;
	depthTarget.samples = VK_SAMPLE_COUNT_1_BIT;
	depthTarget.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthTarget.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthTarget.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthTarget.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthTarget.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthTarget.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorRef{};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthRef{};
	depthRef.attachment = 1;
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpassDesc{};
	subpassDesc.flags = 0;
	subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDesc.inputAttachmentCount = 0;
	subpassDesc.pInputAttachments = nullptr;
	subpassDesc.colorAttachmentCount = 1;
	subpassDesc.pColorAttachments = &colorRef;
	subpassDesc.pResolveAttachments = nullptr;
	subpassDesc.pDepthStencilAttachment = &depthRef;
	subpassDesc.preserveAttachmentCount = 0;
	subpassDesc.pPreserveAttachments = nullptr;

	VkRenderPassCreateInfo rpCI{};
	rpCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpCI.pNext = nullptr;
	rpCI.flags = 0;
	rpCI.attachmentCount = uint32_t(attachments.size());
	rpCI.pAttachments = attachments.data();
	rpCI.subpassCount = 1;
	rpCI.pSubpasses = &subpassDesc;
	rpCI.dependencyCount = 0;
	rpCI.pDependencies = nullptr;

	VkResult result = vkCreateRenderPass(m_device, &rpCI, nullptr, &m_renderPass);
	ThrowIfFailed(result, "vkCreateRenderPass Failed.");
}

void InstancingApp::PrepareFramebuffers()
{
	uint32_t imageCount = m_swapchain->GetImageCount();
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();

	m_framebuffers.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; i++)
	{
		std::vector<VkImageView> views;
		views.push_back(m_swapchain->GetImageView(i));
		views.push_back(m_depthBuffer.view);

		m_framebuffers[i] = CreateFramebuffer(m_renderPass, extent.width, extent.height, uint32_t(views.size()), views.data());
	}
}

void InstancingApp::PrepareTeapot()
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
	m_uniformBuffers.resize(imageCount);

	for (uint32_t i = 0; i < imageCount; ++i)
	{
		uint32_t buffersize = uint32_t(sizeof(ShaderParameters));
		m_uniformBuffers[i] = CreateBuffer(buffersize , VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uboMemoryProps);
	}
}

void InstancingApp::PrepareInstanceData()
{
	VkMemoryPropertyFlags memoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	// インスタンシング用のユニフォームバッファを準備
	uint32_t bufferSize = uint32_t(sizeof(InstanceData)) * InstanceDataMax;
	m_instanceUniforms.resize(m_swapchain->GetImageCount());
	for (BufferObject& ubo : m_instanceUniforms)
	{
		ubo = CreateBuffer(bufferSize, usage, memoryProps);
	}

	std::random_device rnd;
	std::vector<InstanceData> data(InstanceDataMax);

	for (uint32_t i = 0; i < InstanceDataMax; ++i)
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

void InstancingApp::PrepareDescriptors()
{
	// ディスクリプタセットレイアウト
	VkDescriptorSetLayoutBinding descSetLayoutBindings[2];
	descSetLayoutBindings[0].binding = 0;
	descSetLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descSetLayoutBindings[0].descriptorCount = 1;
	descSetLayoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	descSetLayoutBindings[1].binding = 1;
	descSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descSetLayoutBindings[1].descriptorCount = 1;
	descSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayoutCreateInfo descSetLayoutCI{};
	descSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCI.pNext = nullptr;
	descSetLayoutCI.bindingCount = _countof(descSetLayoutBindings);
	descSetLayoutCI.pBindings = descSetLayoutBindings;
	VkResult result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &m_descriptorSetLayout);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");

	// ディスクリプタセット
	uint32_t imageCount = m_swapchain->GetImageCount();
	VkDescriptorSetAllocateInfo descriptorSetAI{};
	descriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAI.pNext = nullptr;
	descriptorSetAI.descriptorPool = m_descriptorPool;
	descriptorSetAI.descriptorSetCount = 1;
	descriptorSetAI.pSetLayouts = &m_descriptorSetLayout;

	for (uint32_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		result = vkAllocateDescriptorSets(m_device, &descriptorSetAI, &descriptorSet);
		ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");

		m_descriptorSets.push_back(descriptorSet);
	}

	// 確保したディスクリプタに書き込む.
	// [0] View,Projの定数バッファ.
	// [1] インスタンシング用のバッファ.
	for (size_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorBufferInfo uniformBufferInfo{};
		uniformBufferInfo.buffer = m_uniformBuffers[i].buffer;
		uniformBufferInfo.offset = 0;
		uniformBufferInfo.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo instanceBufferInfo{};
		instanceBufferInfo.buffer = m_instanceUniforms[i].buffer;
		instanceBufferInfo.offset = 0;
		instanceBufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet descSetSceneUB = book_util::PrepareWriteDescriptorSet(
			m_descriptorSets[i],
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
		);
		descSetSceneUB.pBufferInfo = &uniformBufferInfo;

		VkWriteDescriptorSet descSetInstUB = book_util::PrepareWriteDescriptorSet(
			m_descriptorSets[i],
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

void InstancingApp::CreatePipeline()
{
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

	const VkPipelineColorBlendAttachmentState& blendAttachmentState = book_util::GetOpaqueColorBlendAttachmentState();

	VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
	colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCI.pNext = nullptr;
	colorBlendStateCI.flags = 0;
	colorBlendStateCI.logicOpEnable = VK_FALSE;
	colorBlendStateCI.logicOp = VK_LOGIC_OP_CLEAR;
	colorBlendStateCI.attachmentCount = 1;
	colorBlendStateCI.pAttachments = &blendAttachmentState;
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

	const VkViewport& viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));

	VkOffset2D offset{};
	offset.x = 0;
	offset.y = 0;
	VkRect2D scissor{};
	scissor.offset = offset;
	scissor.extent = extent;

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
		book_util::LoadShader(m_device, "shaderVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
		book_util::LoadShader(m_device, "shaderFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	const VkPipelineRasterizationStateCreateInfo& rasterizerState = book_util::GetDefaultRasterizerState();

	const VkPipelineDepthStencilStateCreateInfo& dsState = book_util::GetDefaultDepthStencilState();

	// DynamicState
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

	// パイプライン構築
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
	pipelineCI.layout = m_pipelineLayout;
	pipelineCI.renderPass = m_renderPass;
	pipelineCI.subpass = 0;
	pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCI.basePipelineIndex = 0;
	VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_pipeline);
	ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

	book_util::DestroyShaderModules(m_device, shaderStages);
}

