#include "DisplayHDR10App.h"
#include "VulkanBookUtil.h"

// �{�ł̓v���W�F�N�g�ݒ�Ń����N�ɉ����Ă邯�ǂ����ł�vulkan_book_1�Ɠ��l��#pragmra�Ń����N�w�肷��
#pragma comment(lib, "vulkan-1.lib")

const int WindowWidth = 800, WindowHeight = 600;
const char* AppTile = "DisplayHDR10";

static void KeyboardInputCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	VulkanAppBase* pApp = book_util::GetApplication<VulkanAppBase>(window);
	if (pApp == nullptr)
	{
		return;
	}

	switch (action)
	{
		case GLFW_PRESS:
			if (key == GLFW_KEY_ESCAPE)
			{
				glfwSetWindowShouldClose(window, GLFW_TRUE);
			}
			if (key == GLFW_KEY_ENTER && mods == GLFW_MOD_ALT)
			{
				pApp->SwitchFullscreen(window);
			}
			break;
		default:
			break;
	}
}

static void MouseMoveCallback(GLFWwindow* window, double x, double y)
{
	VulkanAppBase* pApp = book_util::GetApplication<VulkanAppBase>(window);
	if (pApp == nullptr)
	{
		return;
	}

	static int lastPosX, lastPosY;

	int dx = int(x) - lastPosX;
	int dy = int(y) - lastPosY;
	pApp->OnMouseMove(dx, dy);
	lastPosX = int(x);
	lastPosY = int(y);
}

static void MouseInputCallback(GLFWwindow* window, int button, int action, int mods)
{
	VulkanAppBase* pApp = book_util::GetApplication<VulkanAppBase>(window);
	if (pApp == nullptr)
	{
		return;
	}

	if (action == GLFW_PRESS)
	{
		pApp->OnMouseButtonDown(button);
	}
	if (action == GLFW_RELEASE)
	{
		pApp->OnMouseButtonUp(button);
	}
}

static void MouseWheelCallback(GLFWwindow* window, double xoffset, double yoffset)
{
	VulkanAppBase* pApp = book_util::GetApplication<VulkanAppBase>(window);
	if (pApp == nullptr)
	{
		return;
	}
}

static void WindowResizeCallback(GLFWwindow* window, int width, int height)
{
	VulkanAppBase* pApp = book_util::GetApplication<VulkanAppBase>(window);
	if (pApp == nullptr)
	{
		return;
	}

	pApp->OnSizeChanged(width, height);
}

int _stdcall wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	GLFWwindow* window = glfwCreateWindow(WindowWidth, WindowHeight, AppTile, nullptr, nullptr);

	// �e��R�[���o�b�N�o�^
	glfwSetKeyCallback(window, KeyboardInputCallback);
	glfwSetMouseButtonCallback(window, MouseInputCallback);
	glfwSetCursorPosCallback(window, MouseMoveCallback);
	glfwSetScrollCallback(window, MouseWheelCallback);
	glfwSetWindowSizeCallback(window, WindowResizeCallback);

	DisplayHDR10App theApp;
	glfwSetWindowUserPointer(window, &theApp);

	try
	{
		VkFormat surfaceFormat = VK_FORMAT_R8G8B8A8_UNORM;
		surfaceFormat = VK_FORMAT_R16G16B16_SFLOAT;
		theApp.Initialize(window, surfaceFormat, false);

		while (glfwWindowShouldClose(window) == GLFW_FALSE)
		{
			glfwPollEvents();
			theApp.Render();
		}

		theApp.Terminate();
	}
	catch (std::runtime_error e)
	{
		OutputDebugStringA(e.what());
		OutputDebugStringA("\n");
	}

	glfwTerminate();
	return 0;
}
