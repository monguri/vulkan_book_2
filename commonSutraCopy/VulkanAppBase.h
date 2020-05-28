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
	virtual void OnMouseButtonDown(int button) {}
	virtual void OnMouseButtonUp(int button) {}
	virtual void OnMouseMove(int dx, int dy) {}

	uint32_t GetMemoryTypeIndex(uint32_t requestBits, VkMemoryPropertyFlags requestProps) const;
	void SwitchFullscreen(GLFWwindow* window);

	void Initialize(GLFWwindow* window, VkFormat format, bool isFullscreen);
	void Terminate();

	virtual void Render() = 0;

protected:
	VkDevice m_device = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkInstance m_vkInstance = VK_NULL_HANDLE;

	VkPhysicalDeviceMemoryProperties m_physicalMemProps;
	uint32_t m_gfxQueueIndex = ~0u;

	std::unique_ptr<Swapchain> m_swapchain = VK_NULL_HANDLE;

	GLFWwindow* m_window = nullptr;

private:
	bool m_isMinimizedWindow = false;
	bool m_isFullscreen = false;

	void CreateInstance();
};

