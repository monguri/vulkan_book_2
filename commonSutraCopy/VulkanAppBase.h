#pragma once
#define WIN32_LEAD_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <memory>

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan_win32.h>

#include "Swapchain.h"

class VulkanAppBase
{
public:

	virtual bool OnSizeChanged(uint32_t width, uint32_t height);
	virtual bool OnMouseButtonDown(int button) {}
	virtual bool OnMouseButtonUp(int button) {}
	virtual bool OnMouseMove(int dx, int dy) {}

	uint32_t GetMemoryTypeIndex(uint32_t requestBits, VkMemoryPropertyFlags requestProps) const;
	void SwitchFullscreen(GLFWwindow* window);

protected:
	VkDevice m_device = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	uint32_t m_gfxQueueIndex = ~0u;

	VkPhysicalDeviceMemoryProperties m_physicalMemProps;

	std::unique_ptr<Swapchain> m_swapchain = VK_NULL_HANDLE;

private:
	bool m_isMinimizedWindow = false;
	bool m_isFullscreen = false;
};

