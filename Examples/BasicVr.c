#include "Common.h"

#include "openxr/openxr.h"

static XrInstance instance;
static XrSystemId systemId;
static XrSession session;
static bool doXrFrameLoop = false;
static XrViewConfigurationView *view_configuration_views = NULL;
static Uint32 num_view_configurations;

static int Init(Context* context)
{
	XrResult result;

	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOL, true);
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOL, true);

	if (!SDL_CreateXRGPUDeviceWithProperties(&context->Device, &instance, &systemId, props))
	{
		SDL_Log("SDL_CreateXRGPUDeviceWithProperties failed");
		return -1;
	}

	XrSessionCreateInfo sessionCreateInfo = {XR_TYPE_SESSION_CREATE_INFO};
	result = SDL_CreateGPUXRSession(context->Device, &sessionCreateInfo, &session);
	if(result != XR_SUCCESS)
	{
		SDL_Log("SDL_CreateGPUXRSession failed: %d", result);
		return -1;
	}

	return 0;
}

static int Update(Context* context)
{
	XrResult result;
	XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
	
	result = xrPollEvent(instance, &event);
	if(result == XR_SUCCESS)
	{
		switch(event.type)
		{
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: 
			{
				XrEventDataSessionStateChanged stateChangedEvent = *((XrEventDataSessionStateChanged *)&event);

				switch(stateChangedEvent.state)
				{
					case XR_SESSION_STATE_READY:
					{
						XrSessionBeginInfo sessionBeginInfo = {XR_TYPE_SESSION_BEGIN_INFO};
						sessionBeginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
						result = xrBeginSession(session, &sessionBeginInfo);
						if(result != XR_SUCCESS)
						{
							SDL_Log("Failed to create session: %d", result);
							return -1;
						}

						SDL_Log("Begun OpenXR session");
						
						result = xrEnumerateViewConfigurationViews(
							instance, 
							systemId, 
							XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 
							0, 
							&num_view_configurations, 
							NULL);
						if(result != XR_SUCCESS)
						{
							SDL_Log("Failed to get view configuration array size: %d", result);
							return -1;
						}

						view_configuration_views = SDL_calloc(num_view_configurations, sizeof(XrViewConfigurationView));
						for (Uint32 i = 0; i < num_view_configurations; i++)
							view_configuration_views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
						
						result = xrEnumerateViewConfigurationViews(
							instance, 
							systemId, 
							XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 
							num_view_configurations, 
							&num_view_configurations, 
							view_configuration_views);
						if(result != XR_SUCCESS)
						{
							SDL_Log("Failed to get view configuration data: %d", result);
							return -1;
						}

						for(Uint32 i = 0; i < num_view_configurations; i++)
						{
							SDL_Log(
								"%d max width: %d, max height: %d, max sample count: %d, rec width: %d, rec height: %d, rec sample count: %d",
								i, 
								view_configuration_views->maxImageRectWidth, 
								view_configuration_views->maxImageRectHeight, 
								view_configuration_views->maxSwapchainSampleCount, 
								view_configuration_views->recommendedImageRectWidth, 
								view_configuration_views->recommendedImageRectHeight, 
								view_configuration_views->recommendedSwapchainSampleCount);
						}

						XrSwapchainCreateInfo swapchainCreateInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
						swapchainCreateInfo.width = 1280;
						swapchainCreateInfo.height = 1440;
						swapchainCreateInfo.mipCount = 1;
						swapchainCreateInfo.sampleCount = 1;
						swapchainCreateInfo.faceCount = 1;
						swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT; /* sure..? not sure if anything else is needed. */
						swapchainCreateInfo.arraySize = 1;

						SDL_GPUTextureFormat swapchainFormat;
						XrSwapchain swapchain;
						result = SDL_CreateGPUXRSwapchain(context->Device, session, &swapchainCreateInfo, &swapchainFormat, &swapchain);
						if(result != XR_SUCCESS)
						{
							SDL_Log("Failed to create XR swapchain: %d", result);
							return -1;
						}

						doXrFrameLoop = true;

						break;
					}
					case XR_SESSION_STATE_STOPPING:
					{
						doXrFrameLoop = false;

						result = xrEndSession(session);
						if(result != XR_SUCCESS)
						{
							SDL_Log("Failed to end session: %d", result);
							return -1;
						}

						SDL_Log("Ended OpenXR session");

						break;
					}
					case XR_SESSION_STATE_EXITING:
					{
						/* TODO: handle this gracefully */

						SDL_Log("Session is exiting");

						return -1;
					}
				}

				break;
			}
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			{
				XrEventDataInstanceLossPending instanceLossPending = *((XrEventDataInstanceLossPending *)&event);

				/* TODO: handle this gracefully, probably by re-initializing the GPU device/instance */

				SDL_Log("Instance loss pending, bailing out..");

				return -1;

				break;
			}
		}
	}

	return 0;
}

static int Draw(Context* context)
{
	XrResult result;

	if(doXrFrameLoop) 
	{
		XrFrameWaitInfo frameWaitInfo = {XR_TYPE_FRAME_WAIT_INFO};

		XrFrameState frameState = {XR_TYPE_FRAME_STATE};
		result = xrWaitFrame(session, &frameWaitInfo, &frameState);
		if(result != XR_SUCCESS)
		{
			SDL_Log("Failed to wait on a frame: %d", result);
			return -1;
		}

		XrFrameBeginInfo frameBeginInfo = {XR_TYPE_FRAME_BEGIN_INFO};
		result = xrBeginFrame(session, &frameBeginInfo);
		if(result != XR_SUCCESS)
		{
			SDL_Log("Failed to begin frame: %d", result);
			return -1;
		}

		if(frameState.shouldRender)
		{
			SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(context->Device);
			if (cmdbuf == NULL)
			{
				SDL_Log("AcquireGPUCommandBuffer failed: %s", SDL_GetError());
				return -1;
			}

			SDL_SubmitGPUCommandBuffer(cmdbuf);
		}

		XrFrameEndInfo frameEndInfo = {XR_TYPE_FRAME_END_INFO};
		frameEndInfo.displayTime = frameState.predictedDisplayTime;
		frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		result = xrEndFrame(session, &frameEndInfo);
		if(result != XR_SUCCESS)
		{
			SDL_Log("Failed to end frame: %d", result);
			return -1;
		}
	}

	return 0;
}

static void Quit(Context* context)
{
	if(view_configuration_views != NULL) SDL_free(view_configuration_views);

	CommonQuit(context);
}

Example BasicVr_Example = { "BasicVr", Init, Update, Draw, Quit };
