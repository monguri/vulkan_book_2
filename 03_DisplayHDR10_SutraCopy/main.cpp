#include "DisplayHDR10App.h"

// �{�ł̓v���W�F�N�g�ݒ�Ń����N�ɉ����Ă邯�ǂ����ł�vulkan_book_1�Ɠ��l��#pragmra�Ń����N�w�肷��
#pragma comment(lib, "vulkan-1.lib")

const int WindowWidth = 800, WindowHeight = 600;
const char* AppTile = "DisplayHDR10";

int _stdcall wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	GLFWwindow* window = glfwCreateWindow(WindowWidth, WindowHeight, AppTile, nullptr, nullptr);

	while (glfwWindowShouldClose(window) == GLFW_FALSE)
	{
		glfwPollEvents();
	}

	glfwTerminate();
}

