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

	// �X���b�v�`�F�C������蒼��
	VkFormat format = m_swapchain->GetSurfaceFormat().format;
	m_swapchain->Prepare(m_physicalDevice, m_gfxQueueIndex, width, height, format);
	return true;
}

uint32_t VulkanAppBase::GetMemoryTypeIndex(uint32_t requestBits, VkMemoryPropertyFlags requestProps) const
{
	uint32_t result = ~0u;

	for (uint32_t i = 0; i < m_physicalMemProps.memoryTypeCount; ++i)
	{
		if (requestBits & 1) // i���̃r�b�g��1�̂Ƃ�������r����
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

