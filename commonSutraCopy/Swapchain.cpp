#include "Swapchain.h"
#include "VulkanBookUtil.h"
#include <algorithm>

Swapchain::Swapchain(VkInstance instance, VkDevice device, VkSurfaceKHR surface)
	: m_swapchain(VK_NULL_HANDLE), m_surface(surface), m_vkInstance(instance), m_device(device), m_presentMode(VK_PRESENT_MODE_FIFO_KHR)
{
}

Swapchain::~Swapchain()
{
}

void Swapchain::Prepare(VkPhysicalDevice physDev, uint32_t graphicsQueueIndex, uint32_t width, uint32_t height, VkFormat desireFormat)
{
	VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDev, m_surface, &m_surfaceCaps);
	ThrowIfFailed(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR Failed.");

	uint32_t count = 0;
	result = vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, m_surface, &count, nullptr);
	ThrowIfFailed(result, "vkGetPhysicalDeviceSurfaceFormatsKHR Failed.");
	m_surfaceFormats.resize(count);
	result = vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, m_surface, &count, m_surfaceFormats.data());
	ThrowIfFailed(result, "vkGetPhysicalDeviceSurfaceFormatsKHR Failed.");

	m_selectFormat = VkSurfaceFormatKHR{
		VK_FORMAT_B8G8R8A8_UNORM,
		VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
	};

	for (VkSurfaceFormatKHR f : m_surfaceFormats)
	{
		if (f.format == desireFormat)
		{
			m_selectFormat = f;
		}
	 }

	// サーフェスがサポートされているか確認
	VkBool32 isSupport = VK_FALSE;
	result = vkGetPhysicalDeviceSurfaceSupportKHR(physDev, graphicsQueueIndex, m_surface, &isSupport);
	ThrowIfFailed(result, "vkGetPhysicalDeviceSurfaceSupportKHR Failed.");
	if (isSupport)
	{
		throw book_util::VulkanException("vkGetPhysicalDeviceSurfaceSupportKHR: isSupport = false.");
	}

	uint32_t imageCount = (std::max)(2u, m_surfaceCaps.minImageCount); // windowsのmaxというdefineと間違えられないように()で囲んでいる
	VkExtent2D& extent = m_surfaceCaps.currentExtent;
	if (extent.width == ~0u)
	{
		// 値が無効なのでウィンドウサイズを使用する
		extent.width = uint32_t(width);
		extent.height = uint32_t(height);
	}
	m_surfaceExtent = extent;

	VkSwapchainKHR oldSwapchain = m_swapchain;
	uint32_t queueFamilyIndices[] = {graphicsQueueIndex};

	VkSwapchainCreateInfoKHR ci{};
	ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	ci.pNext = nullptr;
	ci.surface = m_surface;
	ci.minImageCount = imageCount;
	ci.imageFormat = m_selectFormat.format;
	ci.imageColorSpace = m_selectFormat.colorSpace;
	ci.imageExtent = m_surfaceExtent;
	ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	ci.preTransform = m_surfaceCaps.currentTransform;
	ci.imageArrayLayers = 1;
	ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ci.queueFamilyIndexCount = 0;
	ci.pQueueFamilyIndices = queueFamilyIndices;
	ci.presentMode = m_presentMode;
	ci.oldSwapchain = oldSwapchain;
	ci.clipped = VK_TRUE;
	ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

	result = vkCreateSwapchainKHR(m_device, &ci, nullptr, &m_swapchain);
	ThrowIfFailed(result, "vkCreateSwapchainKHR Failed.");

	// TODO:続きの実装
}

