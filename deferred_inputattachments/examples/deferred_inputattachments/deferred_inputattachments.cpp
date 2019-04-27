/*
* Vulkan Example - Deferred shading with multiple render targets (aka G-Buffer) example
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"
#include "VulkanBuffer.hpp"
#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

// Texture properties
#define TEX_DIM 2048
#define TEX_FILTER VK_FILTER_LINEAR

// Offscreen frame buffer properties
#define FB_DIM TEX_DIM

class VulkanExample : public VulkanExampleBase
{
public:
	bool debugDisplay = false;

	struct {
		struct {
			vks::Texture2D colorMap;
			vks::Texture2D normalMap;
		} model;
		struct {
			vks::Texture2D colorMap;
			vks::Texture2D normalMap;
		} floor;
	} textures;

	// Vertex layout for the models
	vks::VertexLayout vertexLayout = vks::VertexLayout({
		vks::VERTEX_COMPONENT_POSITION,
		vks::VERTEX_COMPONENT_UV,
		vks::VERTEX_COMPONENT_COLOR,
		vks::VERTEX_COMPONENT_NORMAL,
		vks::VERTEX_COMPONENT_TANGENT,
	});

	struct {
		vks::Model model;
		vks::Model floor;
		vks::Model quad;
	} models;

	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		glm::vec4 instancePos[3];
	} uboVS, uboOffscreenVS;

	struct Light {
		glm::vec4 position;
		glm::vec3 color;
		float radius;
	};

	struct {
		Light lights[6];
		glm::vec4 viewPos;
	} uboFragmentLights;

	struct {
		vks::Buffer vsFullScreen;
		vks::Buffer vsOffscreen;
		vks::Buffer fsLights;
	} uniformBuffers;

	struct {
		VkPipeline deferred;
		VkPipeline offscreen;
		VkPipeline debug;
	} pipelines;

	struct {
		VkPipelineLayout deferred; 
		VkPipelineLayout offscreen;
	} pipelineLayouts;

	struct {
		VkDescriptorSet model;
		VkDescriptorSet floor;
		std::vector<VkDescriptorSet> attachmentMRT;
	} descriptorSets;

	VkDescriptorSetLayout descriptorSetLayoutDeferred, descriptorSetLayoutMRT;

	// Framebuffer for offscreen rendering
	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
		VkFormat format;
	};

	struct Attachments {
		FrameBufferAttachment position, normal, albedo;
		FrameBufferAttachment depth;
	};

	std::vector<Attachments> attachments;
	// One sampler for the frame buffer color attachments
	VkSampler colorSampler;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Deferred shading (2016 by Sascha Willems)";
		camera.type = Camera::CameraType::firstperson;
		camera.movementSpeed = 5.0f;
#ifndef __ANDROID__
		camera.rotationSpeed = 0.25f;
#endif
		camera.position = { 2.15f, 0.3f, -8.75f };
		camera.setRotation(glm::vec3(-0.75f, 12.5f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		settings.overlay = true;
		UIOverlay.subpass = 1;
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		vkDestroySampler(device, colorSampler, nullptr);

		// Frame buffer

		// Color attachments
		for(auto i = 0; i < attachments.size(); ++i)
		{
			vkDestroyImageView(device, attachments[i].position.view, nullptr);
			vkDestroyImage(device,  attachments[i].position.image, nullptr);
			vkFreeMemory(device,  attachments[i].position.mem, nullptr);

			vkDestroyImageView(device,  attachments[i].normal.view, nullptr);
			vkDestroyImage(device,  attachments[i].normal.image, nullptr);
			vkFreeMemory(device,  attachments[i].normal.mem, nullptr);

			vkDestroyImageView(device,  attachments[i].albedo.view, nullptr);
			vkDestroyImage(device,  attachments[i].albedo.image, nullptr);
			vkFreeMemory(device,  attachments[i].albedo.mem, nullptr);

			// Depth attachment
			vkDestroyImageView(device,  attachments[i].depth.view, nullptr);
			vkDestroyImage(device,  attachments[i].depth.image, nullptr);
			vkFreeMemory(device,  attachments[i].depth.mem, nullptr);
		}
		vkDestroyPipeline(device, pipelines.deferred, nullptr);
		vkDestroyPipeline(device, pipelines.offscreen, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayouts.deferred, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayouts.offscreen, nullptr);

		vkDestroyDescriptorSetLayout(device, descriptorSetLayoutMRT, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayoutDeferred, nullptr);

		// Meshes
		models.model.destroy();
		models.floor.destroy();
		models.quad.destroy();

		// Uniform buffers
		uniformBuffers.vsOffscreen.destroy();
		uniformBuffers.vsFullScreen.destroy();
		uniformBuffers.fsLights.destroy();

		textures.model.colorMap.destroy();
		textures.model.normalMap.destroy();
		textures.floor.colorMap.destroy();
		textures.floor.normalMap.destroy();

	}

	// Enable physical device features required for this example				
	virtual void getEnabledFeatures()
	{
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
		// Enable texture compression  
		if (deviceFeatures.textureCompressionBC) {
			enabledFeatures.textureCompressionBC = VK_TRUE;
		}
		else if (deviceFeatures.textureCompressionASTC_LDR) {
			enabledFeatures.textureCompressionASTC_LDR = VK_TRUE;
		}
		else if (deviceFeatures.textureCompressionETC2) {
			enabledFeatures.textureCompressionETC2 = VK_TRUE;
		}
	};

	// Create a frame buffer attachment
	void createAttachment(
		VkFormat format,  
		VkImageUsageFlagBits usage,
		FrameBufferAttachment *attachment)
	{
		VkImageAspectFlags aspectMask = 0;
		VkImageLayout imageLayout;

		attachment->format = format;

		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		assert(aspectMask > 0);

		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = format;
		image.extent.width = width;
		image.extent.height = height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &attachment->image));
		vkGetImageMemoryRequirements(device, attachment->image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &attachment->mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, attachment->image, attachment->mem, 0));
		
		VkImageViewCreateInfo imageView = vks::initializers::imageViewCreateInfo();
		imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageView.format = format;
		imageView.subresourceRange = {};
		imageView.subresourceRange.aspectMask = aspectMask;
		imageView.subresourceRange.baseMipLevel = 0;
		imageView.subresourceRange.levelCount = 1;
		imageView.subresourceRange.baseArrayLayer = 0;
		imageView.subresourceRange.layerCount = 1;
		imageView.image = attachment->image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageView, nullptr, &attachment->view));
	}

	// Override framebuffer setup from base class
	void setupFrameBuffer()
	{
		std::array<VkImageView, 5> views;

		VkFramebufferCreateInfo frameBufferCI{};
		frameBufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferCI.renderPass = renderPass;
		frameBufferCI.attachmentCount = static_cast<uint32_t>(views.size());
		frameBufferCI.pAttachments = views.data();
		frameBufferCI.width = width;
		frameBufferCI.height = height;
		frameBufferCI.layers = 1;

		frameBuffers.resize(swapChain.imageCount);
		for (uint32_t i = 0; i < frameBuffers.size(); i++)
		{
			views[0] = swapChain.buffers[i].view;
			views[1] = attachments[i].position.view;
			views[2] = attachments[i].normal.view;
  			views[3] = attachments[i].albedo.view;
			views[4] = attachments[i].depth.view;
  			VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCI, nullptr, &frameBuffers[i]));
		}
	}

	// Prepare a new framebuffer and attachments for offscreen rendering (G-Buffer)
	void setupRenderPass()
	{
		VkFormat attDepthFormat;
		VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &attDepthFormat);
		assert(validDepthFormat);
		attachments.resize(swapChain.imageCount);
		for (auto i = 0; i < attachments.size(); ++i) {
			// (World space) Positions
			createAttachment(
				VK_FORMAT_R16G16B16A16_SFLOAT,
				(VkImageUsageFlagBits)(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT),
				&attachments[i].position);

			// (World space) Normals
			createAttachment(
				VK_FORMAT_R16G16B16A16_SFLOAT,
				(VkImageUsageFlagBits)(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT),
				&attachments[i].normal);

			// Albedo (color)
			createAttachment(
				VK_FORMAT_R8G8B8A8_UNORM,
				(VkImageUsageFlagBits)(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT),
				&attachments[i].albedo);

			createAttachment(
				attDepthFormat,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				&attachments[i].depth);
		}
		// Depth attachment

		// Find a suitable depth format

		// Set up separate renderpass with references to the color and depth attachments
		std::array<VkAttachmentDescription, 5> attachmentDescs = {};

		// Init attachment properties
		attachmentDescs[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescs[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescs[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescs[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescs[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescs[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescs[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		for (uint32_t i = 1; i < 5; ++i)
		{
			attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			if (i == 4)
			{
				attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			}
			else
			{
				attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}
		}

		// Formats
		attachmentDescs[0].format = swapChain.colorFormat;
		attachmentDescs[1].format = attachments[0].position.format;
		attachmentDescs[2].format = attachments[0].normal.format;
		attachmentDescs[3].format = attachments[0].albedo.format;
		attachmentDescs[4].format = attachments[0].depth.format;

		std::vector<VkAttachmentReference> colorReferences;
	
 		colorReferences.push_back({ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		colorReferences.push_back({ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		colorReferences.push_back({ 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });


		VkAttachmentReference depthReference = {};
		depthReference.attachment = 4;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		std::array<VkSubpassDescription,2> subpassDescriptions = {};

		/*
			First subpass
			Fill the color and depth attachments
		*/

		subpassDescriptions[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescriptions[0].colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
		subpassDescriptions[0].pColorAttachments = colorReferences.data();
		subpassDescriptions[0].pDepthStencilAttachment = &depthReference;

		/*
			Second subpass
			Input attachment read and swap chain color attachment write
		*/

		// Color reference (target) for this sub pass is the swap chain color attachment
		VkAttachmentReference colorReferenceSwapchain = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		subpassDescriptions[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescriptions[1].colorAttachmentCount = 1;
		subpassDescriptions[1].pColorAttachments = &colorReferenceSwapchain;

		// Color and depth attachment written to in first sub pass will be used as input attachments to be read in the fragment shader
		std::array<VkAttachmentReference,3> inputReferences;
		inputReferences[0] = { 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		inputReferences[1] = { 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		inputReferences[2] = { 3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };


		// Use the attachments filled in the first pass as input attachments
		subpassDescriptions[1].inputAttachmentCount = static_cast<uint32_t>(inputReferences.size());
		subpassDescriptions[1].pInputAttachments = inputReferences.data();

		/*
			Subpass dependencies for layout transitions
		*/
		std::array<VkSubpassDependency, 3> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// This dependency transitions the input attachment from color attachment to shader read
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = 1;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[2].srcSubpass = 0;
		dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.pAttachments = attachmentDescs.data();
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescs.size());
		renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
		renderPassInfo.pSubpasses = subpassDescriptions.data();
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();
	
		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
	

		// Create sampler to sample from the color attachments
		VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_NEAREST;
		sampler.minFilter = VK_FILTER_NEAREST;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 1.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &colorSampler));
	}

	void loadAssets()
	{
		models.model.loadFromFile(getAssetPath() + "models/armor/armor.dae", vertexLayout, 1.0f, vulkanDevice, queue);

		vks::ModelCreateInfo modelCreateInfo;
		modelCreateInfo.scale = glm::vec3(2.0f);
		modelCreateInfo.uvscale = glm::vec2(4.0f);
		modelCreateInfo.center = glm::vec3(0.0f, 2.35f, 0.0f);
		models.floor.loadFromFile(getAssetPath() + "models/plane.obj", vertexLayout, &modelCreateInfo, vulkanDevice, queue);

		// Textures
		std::string texFormatSuffix;
		VkFormat texFormat;
		// Get supported compressed texture format
		if (vulkanDevice->features.textureCompressionBC) {
			texFormatSuffix = "_bc3_unorm";
			texFormat = VK_FORMAT_BC3_UNORM_BLOCK;
		}
		else if (vulkanDevice->features.textureCompressionASTC_LDR) {
			texFormatSuffix = "_astc_8x8_unorm";
			texFormat = VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
		}
		else if (vulkanDevice->features.textureCompressionETC2) {
			texFormatSuffix = "_etc2_unorm";
			texFormat = VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
		}
		else {
			vks::tools::exitFatal("Device does not support any compressed texture format!", VK_ERROR_FEATURE_NOT_PRESENT);
		}

		textures.model.colorMap.loadFromFile(getAssetPath() + "models/armor/color" + texFormatSuffix + ".ktx", texFormat, vulkanDevice, queue);
		textures.model.normalMap.loadFromFile(getAssetPath() + "models/armor/normal" + texFormatSuffix + ".ktx", texFormat, vulkanDevice, queue);
		textures.floor.colorMap.loadFromFile(getAssetPath() + "textures/stonefloor01_color" + texFormatSuffix + ".ktx", texFormat, vulkanDevice, queue);
		textures.floor.normalMap.loadFromFile(getAssetPath() + "textures/stonefloor01_normal" + texFormatSuffix + ".ktx", texFormat, vulkanDevice, queue);
	}

	void reBuildCommandBuffers()
	{
		if (!checkCommandBuffers())
		{
			destroyCommandBuffers();
			createCommandBuffers();
		}
		buildCommandBuffers();
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		std::array<VkClearValue,5> clearValues;
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[3].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[4].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassBeginInfo.pClearValues = clearValues.data();

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			VkDeviceSize offsets[1] = { 0 };

			{
				vks::debugmarker::beginRegion(drawCmdBuffers[i], "Subpass 0: Writing MRT", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
 				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.offscreen);

				// Background
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.offscreen, 0, 1, &descriptorSets.floor, 0, NULL);
				vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.floor.vertices.buffer, offsets);
				vkCmdBindIndexBuffer(drawCmdBuffers[i], models.floor.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(drawCmdBuffers[i], models.floor.indexCount, 1, 0, 0, 0);

				// Object
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.offscreen, 0, 1, &descriptorSets.model, 0, NULL);
				vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.model.vertices.buffer, offsets);
				vkCmdBindIndexBuffer(drawCmdBuffers[i], models.model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(drawCmdBuffers[i], models.model.indexCount, 3, 0, 0, 0);

				vks::debugmarker::endRegion(drawCmdBuffers[i]);
			}

			{
				vks::debugmarker::beginRegion(drawCmdBuffers[i], "Subpass 1: Reading attachments", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
				vkCmdNextSubpass(drawCmdBuffers[i], VK_SUBPASS_CONTENTS_INLINE);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.deferred, 0, 1, &descriptorSets.attachmentMRT[i], 0, NULL);

				// Final composition as full screen quad
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.deferred);
				vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.quad.vertices.buffer, offsets);
				vkCmdBindIndexBuffer(drawCmdBuffers[i], models.quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(drawCmdBuffers[i], 6, 1, 0, 0, 1);

				vks::debugmarker::endRegion(drawCmdBuffers[i]);
			}

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void generateQuads()
	{
		// Setup vertices for multiple screen aligned quads
		// Used for displaying final result and debug 
		struct Vertex {
			float pos[3];
			float uv[2];
			float col[3];
			float normal[3];
			float tangent[3];
		};

		std::vector<Vertex> vertexBuffer;

		float x = 0.0f;
		float y = 0.0f;
		for (uint32_t i = 0; i < 3; i++)
		{
			// Last component of normal is used for debug display sampler index
			vertexBuffer.push_back({ { x+1.0f, y+1.0f, 0.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, (float)i } });
			vertexBuffer.push_back({ { x,      y+1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, (float)i } });
			vertexBuffer.push_back({ { x,      y,      0.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, (float)i } });
			vertexBuffer.push_back({ { x+1.0f, y,      0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, (float)i } });
			x += 1.0f;
			if (x > 1.0f)
			{
				x = 0.0f;
				y += 1.0f;
			}
		}

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			vertexBuffer.size() * sizeof(Vertex),
			&models.quad.vertices.buffer,
			&models.quad.vertices.memory,
			vertexBuffer.data()));

		// Setup indices
		std::vector<uint32_t> indexBuffer = { 0,1,2, 2,3,0 };
		for (uint32_t i = 0; i < 3; ++i)
		{
			uint32_t indices[6] = { 0,1,2, 2,3,0 };
			for (auto index : indices)
			{
				indexBuffer.push_back(i * 4 + index);
			}
		}
		models.quad.indexCount = static_cast<uint32_t>(indexBuffer.size());

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			indexBuffer.size() * sizeof(uint32_t),
			&models.quad.indices.buffer,
			&models.quad.indices.memory,
			indexBuffer.data()));

		models.quad.device = device;
	}

	void setupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] =
			vks::initializers::vertexInputBindingDescription(
				VERTEX_BUFFER_BIND_ID,
				vertexLayout.stride(),
				VK_VERTEX_INPUT_RATE_VERTEX);

		// Attribute descriptions
		vertices.attributeDescriptions.resize(5);
		// Location 0: Position
		vertices.attributeDescriptions[0] =
			vks::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				0);
		// Location 1: Texture coordinates
		vertices.attributeDescriptions[1] =
			vks::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				1,
				VK_FORMAT_R32G32_SFLOAT,
				sizeof(float) * 3);
		// Location 2: Color
		vertices.attributeDescriptions[2] =
			vks::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				2,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 5);
		// Location 3: Normal
		vertices.attributeDescriptions[3] =
			vks::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				3,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 8);
		// Location 4: Tangent
		vertices.attributeDescriptions[4] =
			vks::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				4,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 11);

		vertices.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 9)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vks::initializers::descriptorPoolCreateInfo(
				static_cast<uint32_t>(poolSizes.size()),
				poolSizes.data(),
				10);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetLayout()
	{
		// Deferred layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindingsDeferred =
		{
			// Binding 1 : Position texture target / Scene colormap
			vks::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1),
			// Binding 2 : Normals texture target
			vks::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				2),
			// Binding 3 : Albedo texture target
			vks::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				3),
			// Binding 4 : Fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				4),
		};

		// mrt layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindingsMRT =
		{
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0),
			// Binding 1 : Position texture target / Scene colormap
			vks::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1),
			// Binding 2 : Normals texture target
			vks::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				2),
		};


		VkDescriptorSetLayoutCreateInfo descriptorLayout =
			vks::initializers::descriptorSetLayoutCreateInfo(
				setLayoutBindingsDeferred.data(),
				static_cast<uint32_t>(setLayoutBindingsDeferred.size()));

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayoutDeferred));

		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vks::initializers::pipelineLayoutCreateInfo(
				&descriptorSetLayoutDeferred,
				1);

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayouts.deferred));

		// Offscreen (scene) rendering pipeline layout
		descriptorLayout = 	
			vks::initializers::descriptorSetLayoutCreateInfo(
				setLayoutBindingsMRT.data(),
				static_cast<uint32_t>(setLayoutBindingsMRT.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayoutMRT));
		
		pPipelineLayoutCreateInfo =
			vks::initializers::pipelineLayoutCreateInfo(
				&descriptorSetLayoutMRT,
				1);

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayouts.offscreen));
	}

	void setupDescriptorSet()
	{
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;

		// Textured quad descriptor set
		VkDescriptorSetAllocateInfo allocInfo =
			vks::initializers::descriptorSetAllocateInfo(
				descriptorPool,
				&descriptorSetLayoutDeferred,
				1);

		descriptorSets.attachmentMRT.resize(attachments.size());
		for (auto i = 0; i < descriptorSets.attachmentMRT.size(); ++i)
		{
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.attachmentMRT[i]));
			// Image descriptors for the offscreen color attachments
			VkDescriptorImageInfo texDescriptorPosition =
				vks::initializers::descriptorImageInfo(
					colorSampler,
					attachments[i].position.view,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			VkDescriptorImageInfo texDescriptorNormal =
				vks::initializers::descriptorImageInfo(
					colorSampler,
					attachments[i].normal.view,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			VkDescriptorImageInfo texDescriptorAlbedo =
				vks::initializers::descriptorImageInfo(
					colorSampler,
					attachments[i].albedo.view,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			writeDescriptorSets = {

				// Binding 1 : Position texture target
				vks::initializers::writeDescriptorSet(
					descriptorSets.attachmentMRT[i],
					VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
					1,
					&texDescriptorPosition),
				// Binding 2 : Normals texture target
				vks::initializers::writeDescriptorSet(
					descriptorSets.attachmentMRT[i],
					VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
					2,
					&texDescriptorNormal),
				// Binding 3 : Albedo texture target
				vks::initializers::writeDescriptorSet(
					descriptorSets.attachmentMRT[i],
					VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
					3,
					&texDescriptorAlbedo),
				// Binding 4 : Fragment shader uniform buffer
				vks::initializers::writeDescriptorSet(
					descriptorSets.attachmentMRT[i],
					VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					4,
					&uniformBuffers.fsLights.descriptor),
			};

			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
		}
		// Offscreen (scene)
		allocInfo =
			vks::initializers::descriptorSetAllocateInfo(
				descriptorPool,
				&descriptorSetLayoutMRT,
				1);
		// Model
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.model));
		writeDescriptorSets = 
		{
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(
				descriptorSets.model,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformBuffers.vsOffscreen.descriptor),
			// Binding 1: Color map
			vks::initializers::writeDescriptorSet(
				descriptorSets.model,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				1,
				&textures.model.colorMap.descriptor),
			// Binding 2: Normal map
			vks::initializers::writeDescriptorSet(
				descriptorSets.model,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				2,
				&textures.model.normalMap.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		// Background
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.floor));
		writeDescriptorSets =
		{
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(
				descriptorSets.floor,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformBuffers.vsOffscreen.descriptor),
			// Binding 1: Color map
			vks::initializers::writeDescriptorSet(
				descriptorSets.floor,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				1,
				&textures.floor.colorMap.descriptor),
			// Binding 2: Normal map
			vks::initializers::writeDescriptorSet(
				descriptorSets.floor,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				2,
				&textures.floor.normalMap.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vks::initializers::pipelineInputAssemblyStateCreateInfo(
				VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vks::initializers::pipelineRasterizationStateCreateInfo(
				VK_POLYGON_MODE_FILL,
				VK_CULL_MODE_BACK_BIT,
				VK_FRONT_FACE_CLOCKWISE,
				0);

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			vks::initializers::pipelineColorBlendAttachmentState(
				0xf,
				VK_FALSE);

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			vks::initializers::pipelineColorBlendStateCreateInfo(
				1,
				&blendAttachmentState);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vks::initializers::pipelineDepthStencilStateCreateInfo(
				VK_TRUE, 
				VK_TRUE,
				VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			vks::initializers::pipelineMultisampleStateCreateInfo(
				VK_SAMPLE_COUNT_1_BIT,
				0);

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vks::initializers::pipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				static_cast<uint32_t>(dynamicStateEnables.size()),
				0);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vks::initializers::pipelineCreateInfo(
				pipelineLayouts.deferred,
				renderPass,
				0);

		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();
		
		// Final fullscreen composition pass pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/deferred_inputattachments/deferred.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/deferred_inputattachments/deferred.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Empty vertex input state, quads are generated by the vertex shader
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCreateInfo.pVertexInputState = &emptyInputState;
		pipelineCreateInfo.layout = pipelineLayouts.deferred;
		pipelineCreateInfo.subpass = 1;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.deferred));

		// Offscreen pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/deferred_inputattachments/mrt.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/deferred_inputattachments/mrt.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		// Separate layout
		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		pipelineCreateInfo.layout = pipelineLayouts.offscreen;
		// create pipeline for specific subpass
		pipelineCreateInfo.subpass = 0;
		// Blend attachment states required for all color attachments
		// This is important, as color write mask will otherwise be 0x0 and you
		// won't see anything rendered to the attachment
		std::array<VkPipelineColorBlendAttachmentState, 3> blendAttachmentStates = {
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)			
		};

		colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.size());
		colorBlendState.pAttachments = blendAttachmentStates.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.offscreen));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Fullscreen vertex shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.vsFullScreen,
			sizeof(uboVS)));

		// Deferred vertex shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.vsOffscreen,
			sizeof(uboOffscreenVS)));

		// Deferred fragment shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.fsLights,
			sizeof(uboFragmentLights)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.vsFullScreen.map());
		VK_CHECK_RESULT(uniformBuffers.vsOffscreen.map());
		VK_CHECK_RESULT(uniformBuffers.fsLights.map());

		// Init some values
		uboOffscreenVS.instancePos[0] = glm::vec4(0.0f);
		uboOffscreenVS.instancePos[1] = glm::vec4(-4.0f, 0.0, -4.0f, 0.0f);
		uboOffscreenVS.instancePos[2] = glm::vec4(4.0f, 0.0, -4.0f, 0.0f);

		// Update
		updateUniformBuffersScreen();
		updateUniformBufferDeferredMatrices();
		updateUniformBufferDeferredLights();
	}

	void updateUniformBuffersScreen()
	{
		if (debugDisplay)
		{
			uboVS.projection = glm::ortho(0.0f, 2.0f, 0.0f, 2.0f, -1.0f, 1.0f);
		} 
		else
		{
			uboVS.projection = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
		}
		uboVS.model = glm::mat4(1.0f);

		memcpy(uniformBuffers.vsFullScreen.mapped, &uboVS, sizeof(uboVS));
	}

	void updateUniformBufferDeferredMatrices()
	{
		uboOffscreenVS.projection = camera.matrices.perspective;
		uboOffscreenVS.view = camera.matrices.view;
		uboOffscreenVS.model = glm::mat4(1.0f);

		memcpy(uniformBuffers.vsOffscreen.mapped, &uboOffscreenVS, sizeof(uboOffscreenVS));
	}

	// Update fragment shader light position uniform block
	void updateUniformBufferDeferredLights()
	{
		// White
		uboFragmentLights.lights[0].position = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		uboFragmentLights.lights[0].color = glm::vec3(1.5f);
		uboFragmentLights.lights[0].radius = 15.0f * 0.25f;
		// Red
		uboFragmentLights.lights[1].position = glm::vec4(-2.0f, 0.0f, 0.0f, 0.0f);
		uboFragmentLights.lights[1].color = glm::vec3(1.0f, 0.0f, 0.0f);
		uboFragmentLights.lights[1].radius = 15.0f;
		// Blue
		uboFragmentLights.lights[2].position = glm::vec4(2.0f, 1.0f, 0.0f, 0.0f);
		uboFragmentLights.lights[2].color = glm::vec3(0.0f, 0.0f, 2.5f);
		uboFragmentLights.lights[2].radius = 5.0f;
		// Yellow
		uboFragmentLights.lights[3].position = glm::vec4(0.0f, 0.9f, 0.5f, 0.0f);
		uboFragmentLights.lights[3].color = glm::vec3(1.0f, 1.0f, 0.0f);
		uboFragmentLights.lights[3].radius = 2.0f;
		// Green
		uboFragmentLights.lights[4].position = glm::vec4(0.0f, 0.5f, 0.0f, 0.0f);
		uboFragmentLights.lights[4].color = glm::vec3(0.0f, 1.0f, 0.2f);
		uboFragmentLights.lights[4].radius = 5.0f;
		// Yellow
		uboFragmentLights.lights[5].position = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
		uboFragmentLights.lights[5].color = glm::vec3(1.0f, 0.7f, 0.3f);
		uboFragmentLights.lights[5].radius = 25.0f;

		uboFragmentLights.lights[0].position.x = sin(glm::radians(360.0f * timer)) * 5.0f;
		uboFragmentLights.lights[0].position.z = cos(glm::radians(360.0f * timer)) * 5.0f;

		uboFragmentLights.lights[1].position.x = -4.0f + sin(glm::radians(360.0f * timer) + 45.0f) * 2.0f;
		uboFragmentLights.lights[1].position.z =  0.0f + cos(glm::radians(360.0f * timer) + 45.0f) * 2.0f;

		uboFragmentLights.lights[2].position.x = 4.0f + sin(glm::radians(360.0f * timer)) * 2.0f;
		uboFragmentLights.lights[2].position.z = 0.0f + cos(glm::radians(360.0f * timer)) * 2.0f;

		uboFragmentLights.lights[4].position.x = 0.0f + sin(glm::radians(360.0f * timer + 90.0f)) * 5.0f;
		uboFragmentLights.lights[4].position.z = 0.0f - cos(glm::radians(360.0f * timer + 45.0f)) * 5.0f;

		uboFragmentLights.lights[5].position.x = 0.0f + sin(glm::radians(-360.0f * timer + 135.0f)) * 10.0f;
		uboFragmentLights.lights[5].position.z = 0.0f - cos(glm::radians(-360.0f * timer - 45.0f)) * 10.0f;

		// Current view position
		uboFragmentLights.viewPos = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);

		memcpy(uniformBuffers.fsLights.mapped, &uboFragmentLights, sizeof(uboFragmentLights));
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VulkanExampleBase::submitFrame();
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		generateQuads();
		setupVertexDescriptions();
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();
		updateUniformBufferDeferredLights();
	}

	virtual void viewChanged()
	{
		updateUniformBufferDeferredMatrices();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			if (overlay->checkBox("Display render targets", &debugDisplay)) {
				buildCommandBuffers();
				updateUniformBuffersScreen();
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()
