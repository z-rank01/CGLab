# Legacy VulkanSample

This directory is a protected snapshot of the pre-Render-Graph VulkanSample from commit `0db97b2`.
It keeps the fixed render pass, framebuffer, pipeline, and handwritten barrier path available for rollback and comparison only.

The target is disabled by default. Configure with `-DCGLAB_BUILD_LEGACY_VULKAN_SAMPLE=ON`, then build `VulkanSampleLegacy` explicitly.
The legacy target does not link Render Graph and should not receive new rendering features.
