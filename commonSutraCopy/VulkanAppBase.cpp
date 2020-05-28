#include "VulkanAppBase.h"
#include "VulkanBookUtil.h"

bool VulkanAppBase::OnSizeChanged(uint32_t width, uint32_t height)
{
	m_isMinimizedWindow = (width == 0 || height == 0);
	if (m_isMinimizedWindow)
	{
		return false;
	}
	VkResult result = vkDeviceWaitIdle(m_device);
	ThrowIfFailed(result, "vkDeviceWaitIdle Failed.");

	// スワップチェインを作り直す
	VkFormat format = m_swapchain->GetSurfaceFormat().format;
	m_swapchain->Prepare(m_physicalDevice, m_gfxQueueIndex, width, height, format);
	return true;
}

uint32_t VulkanAppBase::GetMemoryTypeIndex(uint32_t requestBits, VkMemoryPropertyFlags requestProps) const
{
	uint32_t result = ~0u;

	for (uint32_t i = 0; i < m_physicalMemProps.memoryTypeCount; ++i)
	{
		if (requestBits & 1) // i桁のビットが1のときだけ比較する
		{
			const VkMemoryType& type = m_physicalMemProps.memoryTypes[i];
			if ((type.propertyFlags & requestProps) == requestProps)
			{
				result = i;
				break;
			}
		}
		requestBits >>= 1;
	}

	return result;
}

void VulkanAppBase::SwitchFullscreen(GLFWwindow* window)
{
	static int lastWindowPosX, lastWindowPosY;
	static int lastWindowSizeW, lastWindowSizeH;

	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);

	// 現在のモニターに合わせたサイズに変更
	if (!m_isFullscreen)
	{
		// to fullscreen
		glfwGetWindowPos(window, &lastWindowPosX, &lastWindowPosY);
		glfwGetWindowSize(window, &lastWindowSizeW, &lastWindowSizeH);
		glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
	}
	else
	{
		// to windowmode
		glfwSetWindowMonitor(window, nullptr, lastWindowPosX, lastWindowPosY, lastWindowSizeW, lastWindowSizeH, mode->refreshRate);
	}

	m_isFullscreen = !m_isFullscreen;
}

void VulkanAppBase::Initialize(GLFWwindow* window, VkFormat format, bool isFullscreen)
{
}

void VulkanAppBase::Terminate()
{
}

