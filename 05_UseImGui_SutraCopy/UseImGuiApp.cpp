#include "UseImGuiApp.h"
#include "VulkanBookUtil.h"
#include "TeapotModel.h"
#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

void UseImGuiApp::Prepare()
{
	PrepareRenderPass();

	PrepareImGui();

	PrepareDepthbuffer();

	PrepareFramebuffers();

	PrepareCommandBuffersPrimary();

	PrepareTeapot();

	VkPipelineLayoutCreateInfo pipelineLayoutCI{};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.pNext = nullptr;
	pipelineLayoutCI.flags = 0;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &m_descriptorSetLayout;
	pipelineLayoutCI.pushConstantRangeCount = 0;
	pipelineLayoutCI.pPushConstantRanges = nullptr;
	VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &m_pipelineLayout);
	ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");

	CreatePipeline();
}

void UseImGuiApp::Cleanup()
{
	CleanupImGui();

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

	for (const CommandBuffer& c : m_commandBuffers)
	{
		vkDestroyFence(m_device, c.fence, nullptr);
		vkFreeCommandBuffers(m_device, m_commandPool, 1, &c.command);
	}
	m_commandBuffers.clear();
}

void UseImGuiApp::Render()
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
		shaderParam.world = glm::mat4(1.0f);

		glm::vec3 cameraPos = glm::vec3(0.0f, 0.0, 5.0f);
		shaderParam.view = glm::lookAtRH(
			cameraPos,
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f)
		);

		const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
		shaderParam.proj = glm::perspectiveRH(
			glm::radians(45.0f),
			float(extent.width) / float(extent.height),
			0.1f,
			1000.0f
		);

		shaderParam.lightPos = glm::vec4(0.0f, 10.0f, 10.0f, 0.0f);
		shaderParam.cameraPos = glm::vec4(cameraPos, 0.0f);

		const BufferObject& ubo = m_uniformBuffers[imageIndex];
		void* p = nullptr;
		result = vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
		ThrowIfFailed(result, "vkMapMemory Failed.");
		memcpy(p, &shaderParam, sizeof(ShaderParameters));
		vkUnmapMemory(m_device, ubo.memory);
	}

	const VkCommandBuffer& command = m_commandBuffers[imageIndex].command;
	const VkFence& fence = m_commandBuffers[imageIndex].fence;
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
	vkCmdDrawIndexed(command, m_teapot.indexCount, 1, 0, 0, 0);

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

void UseImGuiApp::PrepareImGui()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	bool success = ImGui_ImplGlfw_InitForVulkan(m_window, true);

	ImGui_ImplVulkan_InitInfo info{};
	info.Instance = m_vkInstance;
	info.PhysicalDevice = m_physicalDevice;
	info.Device = m_device;
	info.QueueFamily = m_gfxQueueIndex;
	info.Queue = m_deviceQueue;
	info.DescriptorPool = m_descriptorPool;
	info.MinImageCount = m_swapchain->GetImageCount();
	info.ImageCount = m_swapchain->GetImageCount();
	success = ImGui_ImplVulkan_Init(&info, m_renderPass);

	// フォントテクスチャを転送する
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.pNext = nullptr;
	allocInfo.commandPool = m_commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.pNext = nullptr;
	beginInfo.flags = 0;
	beginInfo.pInheritanceInfo = nullptr;

	VkCommandBuffer command;
	VkResult result = vkAllocateCommandBuffers(m_device, &allocInfo, &command);
	ThrowIfFailed(result, "vkAllocateCommandBuffers Failed.");

	result = vkBeginCommandBuffer(command, &beginInfo);
	ThrowIfFailed(result, "vkBeginCommandBuffer Failed.");
	ImGui_ImplVulkan_CreateFontsTexture(command);
	result = vkEndCommandBuffer(command);
	ThrowIfFailed(result, "vkEndCommandBuffer Failed.");

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = nullptr;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &m_presentCompletedSem;
	submitInfo.pWaitDstStageMask = nullptr;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &command;
	submitInfo.signalSemaphoreCount = 0;
	submitInfo.pSignalSemaphores = nullptr;
	result = vkQueueSubmit(m_deviceQueue, 1, &submitInfo, VK_NULL_HANDLE);
	ThrowIfFailed(result, "vkQueueSubmit Failed.");

	// フォントテクスチャ転送の完了を待つ
	result = vkDeviceWaitIdle(m_device);
	ThrowIfFailed(result, "vkDeviceWaitIdle Failed.");
	vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
}

void UseImGuiApp::CleanupImGui()
{
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

void UseImGuiApp::RenderImGui(const VkCommandBuffer& command)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// ImGuiウィジェットを描画する
	ImGui::Begin("Information");
	ImGui::Text("Hello, ImGui world");
	ImGui::Text("Framerate(avg) %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

	if (ImGui::Button("Button"))
	{
		// ボタンが押下されたときの処理.
	}
	ImGui::SliderFloat("Factor", &m_factor, 0.0f, 100.0f);
	ImGui::ColorPicker4("Color", m_color);
	ImGui::End();

	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

bool UseImGuiApp::OnSizeChanged(uint32_t width, uint32_t height)
{
	bool isResized = VulkanAppBase::OnSizeChanged(width, height);
	if (isResized)
	{
		// 古いデプスバッファを破棄
		DestroyImage(m_depthBuffer);

		// 古いフレームバッファを破棄
		DestroyFramebuffers(uint32_t(m_framebuffers.size()), m_framebuffers.data());

		// 新解像度でのデプスバッファ作成
		PrepareDepthbuffer();

		// 新解像度でのフレームバッファを作成
		PrepareFramebuffers();
	}

	return isResized;
}

void UseImGuiApp::PrepareRenderPass()
{
	std::array<VkAttachmentDescription, 2> attachments;
	attachments[0] = book_util::GetAttachmentDescription(
		m_swapchain->GetSurfaceFormat().format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	);
	attachments[1] = book_util::GetAttachmentDescription(
		VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	);

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

void UseImGuiApp::PrepareDepthbuffer()
{
	// デプスバッファを準備する
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
	m_depthBuffer = CreateImage(extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void UseImGuiApp::PrepareFramebuffers()
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

void UseImGuiApp::PrepareCommandBuffersPrimary()
{
	uint32_t imageCount = m_swapchain->GetImageCount();
	m_commandBuffers.resize(imageCount);

	VkFenceCreateInfo fenceCI{};
	fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCI.pNext = nullptr;
	fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.pNext = nullptr;
	allocInfo.commandPool = m_commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	for (uint32_t i = 0; i < imageCount; i++)
	{
		VkResult result = vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffers[i].command);
		ThrowIfFailed(result, "vkAllocateCommandBuffers Failed.");
		result = vkCreateFence(m_device, &fenceCI, nullptr, &m_commandBuffers[i].fence);
		ThrowIfFailed(result, "vkCreateFence Failed.");
	}
}

void UseImGuiApp::PrepareTeapot()
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

	// ディスクリプタセットレイアウト
	VkDescriptorSetLayoutBinding descSetLayoutBindings[1];

	VkDescriptorSetLayoutBinding bindingUBO{};
	bindingUBO.binding = 0;
	bindingUBO.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindingUBO.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	bindingUBO.descriptorCount = 1;
	descSetLayoutBindings[0] = bindingUBO;

	VkDescriptorSetLayoutCreateInfo descSetLayoutCI{};
	descSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCI.pNext = nullptr;
	descSetLayoutCI.bindingCount = _countof(descSetLayoutBindings);
	descSetLayoutCI.pBindings = descSetLayoutBindings;
	VkResult result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &m_descriptorSetLayout);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");

	// ディスクリプタセット
	VkDescriptorSetAllocateInfo descriptorSetAI{};
	descriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAI.pNext = nullptr;
	descriptorSetAI.descriptorPool = m_descriptorPool;
	descriptorSetAI.descriptorSetCount = 1;
	descriptorSetAI.pSetLayouts = &m_descriptorSetLayout;

	uint32_t imageCount = m_swapchain->GetImageCount();
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		result = vkAllocateDescriptorSets(m_device, &descriptorSetAI, &descriptorSet);
		ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");

		m_descriptorSets.push_back(descriptorSet);
	}

	// 定数バッファの準備
	VkMemoryPropertyFlags uboMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	m_uniformBuffers.resize(imageCount);

	for (uint32_t i = 0; i < imageCount; ++i)
	{
		uint32_t buffersize = uint32_t(sizeof(ShaderParameters));
		m_uniformBuffers[i] = CreateBuffer(buffersize , VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uboMemoryProps);
	}

	for (size_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = m_uniformBuffers[i].buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet writeDescSet{};
		writeDescSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescSet.pNext = nullptr;
		writeDescSet.dstSet = m_descriptorSets[i],
		writeDescSet.dstBinding = 0;
		writeDescSet.dstArrayElement = 0;
		writeDescSet.descriptorCount = 1;
		writeDescSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writeDescSet.pImageInfo = nullptr;
		writeDescSet.pBufferInfo = &bufferInfo;
		writeDescSet.pTexelBufferView = nullptr;

		vkUpdateDescriptorSets(m_device, 1, &writeDescSet, 0, nullptr);
	}
}

void UseImGuiApp::CreatePipeline()
{
	// 頂点の入力の設定
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

