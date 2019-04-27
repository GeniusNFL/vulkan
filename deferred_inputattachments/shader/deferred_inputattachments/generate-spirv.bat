#!/bin/bash
/home/liningfei/vulkan/VulkanSDK/1.1.92.1/x86_64/bin/glslangValidator -V debug.vert -o debug.vert.spv
/home/liningfei/vulkan/VulkanSDK/1.1.92.1/x86_64/bin/glslangValidator  -V debug.frag -o debug.frag.spv
/home/liningfei/vulkan/VulkanSDK/1.1.92.1/x86_64/bin/glslangValidator  -V deferred.vert -o deferred.vert.spv
/home/liningfei/vulkan/VulkanSDK/1.1.92.1/x86_64/bin/glslangValidator  -V deferred.frag -o deferred.frag.spv
/home/liningfei/vulkan/VulkanSDK/1.1.92.1/x86_64/bin/glslangValidator  -V mrt.vert -o mrt.vert.spv
/home/liningfei/vulkan/VulkanSDK/1.1.92.1/x86_64/bin/glslangValidator  -V mrt.frag -o mrt.frag.spv

