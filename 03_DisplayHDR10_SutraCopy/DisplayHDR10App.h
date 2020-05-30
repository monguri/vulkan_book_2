#pragma once
#include "VulkanAppBase.h"

class DisplayHDR10App : public VulkanAppBase
{
public:
	virtual void Prepare() override;
	virtual void Cleanup() override;
	virtual void Render() override;

private:
	VkRenderPass m_renderPass;

	void CreateRenderPass();
};

