#include "VulkanAppBase.h"
#include "VulkanBookUtil.h"

#include "sstream"

static VkBool32 VKAPI_CALL DebugReportCallback(
	VkDebugReportFlagsEXT flags,
	VkDebugReportObjectTypeEXT objectType,
	uint64_t object,
	size_t location,
	int32_t messageCode,
	const char* pLayerPrefix,
	const char* pMessage,
	void* pUserData)
{
	VkBool32 ret = VK_FALSE;
	if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT || flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
	{
		ret = VK_TRUE;
	}

	std::stringstream ss;
	if (pLayerPrefix)
	{
		ss << "[" << pLayerPrefix << "] ";
	}
	ss << pMessage << std::endl;

	OutputDebugStringA(ss.str().c_str());

	return ret;

}

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

void VulkanAppBase::SwitchFullscreen(GLFWwindow* window)
{
	static int lastWindowPosX, lastWindowPosY;
	static int lastWindowSizeW, lastWindowSizeH;

	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);

	// ���݂̃��j�^�[�ɍ��킹���T�C�Y�ɕύX
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
	m_window = window;
	CreateInstance();

	// �����f�o�C�X�̑I��
	uint32_t count = 0;
	VkResult result = vkEnumeratePhysicalDevices(m_vkInstance, &count, nullptr);
	ThrowIfFailed(result, "vkEnumeratePhysicalDevices Failed.");
	std::vector<VkPhysicalDevice> physicalDevices(count);
	result = vkEnumeratePhysicalDevices(m_vkInstance, &count, physicalDevices.data());
	ThrowIfFailed(result, "vkEnumeratePhysicalDevices Failed.");

	// �ŏ��̃f�o�C�X���g�p����
	m_physicalDevice = physicalDevices[0];
	vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_physicalMemProps);

	// �O���t�B�b�N�X�̃L���[�C���f�b�N�X�l���擾.
	SelectGraphicsQueue();

#ifdef _DEBUG
	EnableDebugReport();
#endif
}

void VulkanAppBase::Terminate()
{
}

void VulkanAppBase::CreateInstance()
{
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "VulkanBook2";
	appInfo.pEngineName = "VulkanBook2";
	appInfo.apiVersion = VK_API_VERSION_1_1;
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);

	// �C���X�^���X�g�����̎擾
	uint32_t count = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
	std::vector<VkExtensionProperties> props(count);
	vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data());

	std::vector<const char*> extensions;
	extensions.reserve(count);
	for (const VkExtensionProperties& v : props)
	{
		extensions.push_back(v.extensionName);
	}

	VkInstanceCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ci.enabledExtensionCount = uint32_t(extensions.size());
	ci.ppEnabledExtensionNames = extensions.data();
	ci.pApplicationInfo = &appInfo;
#ifdef _DEBUG
	// �f�o�b�O�r���h�ł͌��؃��C���[��L����
	const char* layers[] = { "VK_LAYER_LUNARG_standard_validation" };
	ci.enabledLayerCount = 1;
	ci.ppEnabledLayerNames = layers;
#endif

	VkResult result = vkCreateInstance(&ci, nullptr, &m_vkInstance);
	ThrowIfFailed(result, "vkCreateInstance Failed.");
}

void VulkanAppBase::SelectGraphicsQueue()
{
	uint32_t queuePropCount;
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queuePropCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilyProps(queuePropCount);
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queuePropCount, queueFamilyProps.data());

	uint32_t graphicsQueue = 0;
	for (uint32_t i = 0; i < queuePropCount; ++i)
	{
		if (queueFamilyProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			graphicsQueue = i;
			break;
		}
	}

	m_gfxQueueIndex = graphicsQueue;
}

#define GetInstanceProcAddr(FuncName) \
m_##FuncName = reinterpret_cast<PFN_##FuncName>(vkGetInstanceProcAddr(m_vkInstance, #FuncName))

void VulkanAppBase::EnableDebugReport()
{
	GetInstanceProcAddr(vkCreateDebugReportCallbackEXT);
	GetInstanceProcAddr(vkDebugReportMessageEXT);
	GetInstanceProcAddr(vkDestroyDebugReportCallbackEXT);

	VkDebugReportFlagsEXT flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;

	VkDebugReportCallbackCreateInfoEXT drcCI{};
	drcCI.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
	drcCI.flags = flags;
	drcCI.pfnCallback = &DebugReportCallback;

	VkResult result = m_vkCreateDebugReportCallbackEXT(m_vkInstance, &drcCI, nullptr, &m_debugReport);
	ThrowIfFailed(result, "vkCreateDebugReportCallbackEXT Failed.");
}

