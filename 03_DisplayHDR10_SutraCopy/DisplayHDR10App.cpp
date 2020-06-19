#include "DisplayHDR10App.h"
#include "VulkanBookUtil.h"
#include "TeapotModel.h"
#include <array>

void DisplayHDR10App::Prepare()
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
}

void DisplayHDR10App::Cleanup()
{
	DestroyBuffer(m_teapot.vertexBuffer);
	DestroyBuffer(m_teapot.indexBuffer);

	vkFreeDescriptorSets(m_device, m_descriptorPool, uint32_t(m_descriptorSets.size()), m_descriptorSets.data());
	m_descriptorSets.clear();

	vkDestroyPipeline(m_device, m_pipeline, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
	vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

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
}

void DisplayHDR10App::Render()
{
}

bool DisplayHDR10App::OnSizeChanged(uint32_t width, uint32_t height)
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

void DisplayHDR10App::CreateRenderPass()
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

void DisplayHDR10App::PrepareFramebuffers()
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

void DisplayHDR10App::PrepareTeapot()
{
	// ステージ用のVBとIB、ターゲットのVBとIBの用意
	uint32_t bufferSizeVB = uint32_t(sizeof(TeapotModel::TeapotVerticesPN));
	VkBufferUsageFlags usageVB = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkMemoryPropertyFlags srcMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	VkMemoryPropertyFlags dstMemoryProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	const BufferObject& stageVB = CreateBuffer(bufferSizeVB, usageVB, srcMemoryProps);
	const BufferObject& targetVB = CreateBuffer(bufferSizeVB, usageVB, dstMemoryProps);

	uint32_t bufferSizeIB = uint32_t(sizeof(TeapotModel::TeapotIndices));
	VkBufferUsageFlags usageIB = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	const BufferObject& stageIB = CreateBuffer(bufferSizeIB, usageIB, srcMemoryProps);
	const BufferObject& targetIB = CreateBuffer(bufferSizeIB, usageIB, dstMemoryProps);

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
		m_uniformBuffers[i] = CreateBuffer(sizeof(ShaderParameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uboMemoryProps);
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

void DisplayHDR10App::CreatePipeline()
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
	inputAttribs[1].offset = offsetof(TeapotModel::Vertex, Position);

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
	pipelineCI.pDynamicState = nullptr;
	pipelineCI.layout = m_pipelineLayout;
	pipelineCI.renderPass = m_renderPass;
	pipelineCI.subpass = 0;
	pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCI.basePipelineIndex = 0;
	VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_pipeline);
	ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

	book_util::DestroyShaderModules(m_device, shaderStages);
}

