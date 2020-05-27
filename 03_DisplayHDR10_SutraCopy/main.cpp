#include "DisplayHDR10App.h"

// 本ではプロジェクト設定でリンクに加えてるけどここではvulkan_book_1と同様に#pragmraでリンク指定する
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

