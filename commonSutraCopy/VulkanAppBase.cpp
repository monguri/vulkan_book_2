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
	m_window = window;
	CreateInstance();

	// 物理デバイスの選択
	uint32_t count = 0;
	VkResult result = vkEnumeratePhysicalDevices(m_vkInstance, &count, nullptr);
	ThrowIfFailed(result, "vkEnumeratePhysicalDevices Failed.");
	std::vector<VkPhysicalDevice> physicalDevices(count);
	result = vkEnumeratePhysicalDevices(m_vkInstance, &count, physicalDevices.data());
	ThrowIfFailed(result, "vkEnumeratePhysicalDevices Failed.");

	// 最初のデバイスを使用する
	m_physicalDevice = physicalDevices[0];
	vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_physicalMemProps);

	// グラフィックスのキューインデックス値を取得.
	SelectGraphicsQueue();

#ifdef _DEBUG
	EnableDebugReport();
#endif

	// 論理デバイスの生成
	CreateDevice();

	// コマンドプールの生成
	CreateCommandPool();

	VkSurfaceKHR surface = VK_NULL_HANDLE;
	result = glfwCreateWindowSurface(m_vkInstance, window, nullptr, &surface);
	ThrowIfFailed(result, "glfwCreateWindowSurface Failed.");

	// スワップチェインの生成
	m_swapchain = std::make_unique<Swapchain>(m_vkInstance, m_device, surface);
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

	// インスタンス拡張情報の取得
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
	// デバッグビルドでは検証レイヤーを有効化
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

void VulkanAppBase::CreateDevice()
{
	const float defaultQueuePriority(1.0f);
	VkDeviceQueueCreateInfo devQueueCI{};
	devQueueCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	devQueueCI.pNext = nullptr;
	devQueueCI.flags = 0;
	devQueueCI.queueFamilyIndex = m_gfxQueueIndex;
	devQueueCI.queueCount = 1;
	devQueueCI.pQueuePriorities = &defaultQueuePriority;


	// デバイス拡張情報をここでも取得
	std::vector<VkExtensionProperties> deviceExtensions;

	uint32_t count = 0;
	VkResult result = vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &count, nullptr);
	ThrowIfFailed(result, "vkEnumerateDeviceExtensionProperties Failed.");
	deviceExtensions.resize(count);
	result = vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &count, deviceExtensions.data());
	ThrowIfFailed(result, "vkEnumerateDeviceExtensionProperties Failed.");

	std::vector<const char*> extensions;
	extensions.reserve(count);
	for (const VkExtensionProperties& v : deviceExtensions)
	{
		extensions.push_back(v.extensionName);
	}

	VkDeviceCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	ci.pNext = nullptr;
	ci.flags = 0;
	ci.queueCreateInfoCount = 1;
	ci.pQueueCreateInfos = &devQueueCI;
	ci.enabledLayerCount = 0;
	ci.ppEnabledLayerNames = nullptr;
	ci.enabledExtensionCount = uint32_t(extensions.size());
	ci.ppEnabledExtensionNames = extensions.data();
	ci.pEnabledFeatures = nullptr;

	result = vkCreateDevice(m_physicalDevice, &ci, nullptr, &m_device);
	ThrowIfFailed(result, "vkCreateDevice Failed.");

	vkGetDeviceQueue(m_device, m_gfxQueueIndex, 0, &m_deviceQueue);
}

void VulkanAppBase::CreateCommandPool()
{
	VkCommandPoolCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	ci.pNext = nullptr;
	ci.queueFamilyIndex = m_gfxQueueIndex;
	ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	VkResult result = vkCreateCommandPool(m_device, &ci, nullptr, &m_commandPool);
	ThrowIfFailed(result, "vkCreateCommandPool Failed.");
}

