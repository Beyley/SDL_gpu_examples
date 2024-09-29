#include "Common.h"

#include "openxr/openxr.h"

static XrInstance instance;
static XrSystemId systemId;
static XrSession session;
static bool doXrFrameLoop = false;

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
	session = SDL_CreateGPUXRSession(context->Device, &sessionCreateInfo, &result);
	if(!session)
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
							SDL_Log("Failed to create session, reason %d", result);
							return -1;
						}

						doXrFrameLoop = true;

						SDL_Log("Begun OpenXR session");
						
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
	CommonQuit(context);
}

Example BasicVr_Example = { "BasicVr", Init, Update, Draw, Quit };
