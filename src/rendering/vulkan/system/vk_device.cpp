// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2018 Christoph Oelckers
// Copyright(C) 2019 Magnus Norddahl
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "volk/volk.h"

#ifdef _WIN32
#undef max
#undef min

extern HWND Window;
#endif

#include <vector>
#include <array>
#include <set>

#include "vk_device.h"
#include "vk_swapchain.h"
#include "c_cvars.h"
#include "i_system.h"
#include "version.h"
#include "doomerrors.h"

#ifdef NDEBUG
CVAR(Bool, vk_debug, true, 0);	// this should be false, once the oversized model can be removed.
#else
CVAR(Bool, vk_debug, true, 0);
#endif

VulkanDevice::VulkanDevice()
{
	if (volkInitialize() != VK_SUCCESS)
	{
		throw std::runtime_error("Unable to find Vulkan");
	}
	auto iver = volkGetInstanceVersion();
	if (iver == 0)
	{
		throw std::runtime_error("Vulkan not supported");
	}

	try
	{
		createInstance();
		createSurface();
		selectPhysicalDevice();
		createDevice();
		createAllocator();

		RECT clientRect = { 0 };
		GetClientRect(Window, &clientRect);
		swapChain = std::make_unique<VulkanSwapChain>(this, clientRect.right, clientRect.bottom, true);

		createSemaphores();
	}
	catch (...)
	{
		releaseResources();
		throw;
	}
}

VulkanDevice::~VulkanDevice()
{
	releaseResources();
}

void VulkanDevice::windowResized()
{
	RECT clientRect = { 0 };
	GetClientRect(Window, &clientRect);

	swapChain.reset();
	swapChain = std::make_unique<VulkanSwapChain>(this, clientRect.right, clientRect.bottom, true);
}

void VulkanDevice::waitPresent()
{
	vkWaitForFences(device, 1, &renderFinishedFence, VK_TRUE, std::numeric_limits<uint64_t>::max());
	vkResetFences(device, 1, &renderFinishedFence);
}

void VulkanDevice::beginFrame()
{
	VkResult result = vkAcquireNextImageKHR(device, swapChain->swapChain, std::numeric_limits<uint64_t>::max(), imageAvailableSemaphore, VK_NULL_HANDLE, &presentImageIndex);
	if (result != VK_SUCCESS)
		throw std::runtime_error("Failed to acquire next image!");
}

void VulkanDevice::presentFrame()
{
	VkSemaphore waitSemaphores[] = { renderFinishedSemaphore };
	VkSwapchainKHR swapChains[] = { swapChain->swapChain };

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = waitSemaphores;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &presentImageIndex;
	presentInfo.pResults = nullptr;
	vkQueuePresentKHR(presentQueue, &presentInfo);
}

VkBool32 VulkanDevice::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
{
	VulkanDevice *device = (VulkanDevice*)userData;

	const char *prefix = "";
	if (messageSeverity & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	{
		prefix = "error";
	}
	else if (messageSeverity & VK_DEBUG_REPORT_WARNING_BIT_EXT)
	{
		prefix = "warning";
	}

	Printf("Vulkan validation layer %s: %s\n", prefix, callbackData->pMessage);

	return VK_FALSE;
}

void VulkanDevice::createInstance()
{
	VkResult result;

	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
	availableLayers.resize(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	uint32_t extensionCount = 0;
	result = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
	extensions.resize(extensionCount);
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "GZDoom";
	appInfo.applicationVersion = VK_MAKE_VERSION(VER_MAJOR, VER_MINOR, VER_REVISION);
	appInfo.pEngineName = "GZDoom";
	appInfo.engineVersion = VK_MAKE_VERSION(ENG_MAJOR, ENG_MINOR, ENG_REVISION);
	appInfo.apiVersion = VK_API_VERSION_1_0;

	std::vector<const char *> enabledExtensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };

	std::vector<const char*> validationLayers;
	std::string debugLayer = "VK_LAYER_LUNARG_standard_validation";
	bool debugLayerFound = false;
	for (const VkLayerProperties &layer : availableLayers)
	{
		if (layer.layerName == debugLayer)
		{
			validationLayers.push_back(debugLayer.c_str());
			//enabledExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
			enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			debugLayerFound = true;
		}
	}

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
	createInfo.enabledLayerCount = (uint32_t)validationLayers.size();
	createInfo.ppEnabledLayerNames = validationLayers.data();
	createInfo.ppEnabledExtensionNames = enabledExtensions.data();
	createInfo.enabledLayerCount = 0;

	result = vkCreateInstance(&createInfo, nullptr, &instance);
	if (result != VK_SUCCESS)
		throw std::runtime_error("Could not create vulkan instance");

	volkLoadInstance(instance);

	if (debugLayerFound)
	{
		VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = debugCallback;
		createInfo.pUserData = this;
		result = vkCreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger);
		if (result != VK_SUCCESS)
			throw std::runtime_error("vkCreateDebugUtilsMessengerEXT failed");

		/*
		VkDebugReportCallbackCreateInfoEXT createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
		createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
		createInfo.pfnCallback = debugCallback;
		result = vkCreateDebugReportCallbackEXT(instance, &createInfo, nullptr, &vkCallback);
		if (result != VK_SUCCESS)
			throw std::runtime_error("vkCreateDebugReportCallbackEXT failed");
		*/
	}
}

void VulkanDevice::createSurface()
{
#ifdef _WIN32
	VkWin32SurfaceCreateInfoKHR windowCreateInfo;
	windowCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	windowCreateInfo.hwnd = Window;
	windowCreateInfo.hinstance = GetModuleHandle(nullptr);

	VkResult result = vkCreateWin32SurfaceKHR(instance, &windowCreateInfo, nullptr, &surface);
	if (result != VK_SUCCESS)
		throw std::runtime_error("Could not create vulkan surface");
#elif defined __APPLE__
	// todo
#else
	// todo
#endif
}

void VulkanDevice::selectPhysicalDevice()
{
	VkResult result;

	uint32_t deviceCount = 0;
	result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
	if (result != VK_SUCCESS)
		throw std::runtime_error("vkEnumeratePhysicalDevices failed");
	else if (deviceCount == 0)
		throw std::runtime_error("Could not find any vulkan devices");

	std::vector<VkPhysicalDevice> devices(deviceCount);
	result = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
	if (result != VK_SUCCESS)
		throw std::runtime_error("vkEnumeratePhysicalDevices failed (2)");

	for (const auto &device : devices)
	{
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(device, &deviceProperties);

		VkPhysicalDeviceFeatures deviceFeatures;
		vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

		bool isUsableDevice = deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && deviceFeatures.geometryShader && deviceFeatures.samplerAnisotropy;
		if (!isUsableDevice)
			continue;

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

		graphicsFamily = -1;
		computeFamily = -1;
		transferFamily = -1;
		sparseBindingFamily = -1;
		presentFamily = -1;

		int i = 0;
		for (const auto& queueFamily : queueFamilies)
		{
			// Only accept a decent GPU for now..
			VkQueueFlags gpuFlags = (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT);
			if (queueFamily.queueCount > 0 && (queueFamily.queueFlags & gpuFlags) == gpuFlags)
			{
				graphicsFamily = i;
				computeFamily = i;
				transferFamily = i;
				sparseBindingFamily = i;
			}

			VkBool32 presentSupport = false;
			result = vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
			if (result == VK_SUCCESS && queueFamily.queueCount > 0 && presentSupport) presentFamily = i;

			i++;
		}

		uint32_t deviceExtensionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &deviceExtensionCount, nullptr);
		availableDeviceExtensions.resize(deviceExtensionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &deviceExtensionCount, availableDeviceExtensions.data());

		std::set<std::string> requiredExtensionSearch(requiredExtensions.begin(), requiredExtensions.end());
		for (const auto &ext : availableDeviceExtensions)
			requiredExtensionSearch.erase(ext.extensionName);
		if (!requiredExtensionSearch.empty())
			continue;

		physicalDevice = device;
		return;
	}

	throw std::runtime_error("No Vulkan device supports the minimum requirements of this application");
}

void VulkanDevice::createDevice()
{
	float queuePriority = 1.0f;
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

	std::set<int> neededFamilies;
	neededFamilies.insert(graphicsFamily);
	neededFamilies.insert(presentFamily);
	neededFamilies.insert(computeFamily);

	for (int index : neededFamilies)
	{
		VkDeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = index;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back(queueCreateInfo);
	}

	VkPhysicalDeviceFeatures usedDeviceFeatures = {};
	usedDeviceFeatures.samplerAnisotropy = VK_TRUE;

	VkDeviceCreateInfo deviceCreateInfo = {};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
	deviceCreateInfo.pEnabledFeatures = &usedDeviceFeatures;
	deviceCreateInfo.enabledExtensionCount = (uint32_t)requiredExtensions.size();
	deviceCreateInfo.ppEnabledExtensionNames = requiredExtensions.data();
	deviceCreateInfo.enabledLayerCount = 0;

	VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
	if (result != VK_SUCCESS)
		throw std::runtime_error("Could not create vulkan device");

	volkLoadDevice(device);

	vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
	vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
}

void VulkanDevice::createAllocator()
{
	VmaAllocatorCreateInfo allocinfo = {};
	// allocinfo.flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT; // To do: enable this for better performance
	allocinfo.physicalDevice = physicalDevice;
	allocinfo.device = device;
	allocinfo.preferredLargeHeapBlockSize = 64 * 1024 * 1024;
	if (vmaCreateAllocator(&allocinfo, &allocator) != VK_SUCCESS)
		throw std::runtime_error("Unable to create allocator");
}

void VulkanDevice::createSemaphores()
{
	VkSemaphoreCreateInfo semaphoreInfo = {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkResult result = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore);
	if (result != VK_SUCCESS)
		throw std::runtime_error("Failed to create semaphore!");

	result = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore);
	if (result != VK_SUCCESS)
		throw std::runtime_error("Failed to create semaphore!");

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	result = vkCreateFence(device, &fenceInfo, nullptr, &renderFinishedFence);
	if (result != VK_SUCCESS)
		throw std::runtime_error("Failed to create fence!");
}

void VulkanDevice::releaseResources()
{
	if (device)
		vkDeviceWaitIdle(device);

	if (!imageAvailableSemaphore)
		vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
	imageAvailableSemaphore = 0;

	if (!renderFinishedSemaphore)
		vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
	renderFinishedSemaphore = 0;

	if (!renderFinishedFence)
		vkDestroyFence(device, renderFinishedFence, nullptr);
	renderFinishedFence = 0;

	swapChain.reset();

	if (device)
		vkDestroyDevice(device, nullptr);
	device = nullptr;

	if (surface)
		vkDestroySurfaceKHR(instance, surface, nullptr);
	surface = 0;

	if (instance)
		vkDestroyInstance(instance, nullptr);
	instance = nullptr;
}

uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
	{
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			return i;
	}

	throw std::runtime_error("failed to find suitable memory type!");
}
