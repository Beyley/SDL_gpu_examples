#include "Common.h"

static XrInstance xr_instance;
static XrSystemId xr_system;
static XrSession xr_session;

static int Init(Context* context)
{
	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOL, true);
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOL, true);

	if (!SDL_CreateVRGPUDeviceWithProperties(&context->Device, &xr_instance, &xr_system, &xr_session, props))
	{
		SDL_Log("SDL_CreateVRGPUDeviceWithProperties failed");
		return -1;
	}

	return 0;
}

static int Update(Context* context)
{
	return 0;
}

static int Draw(Context* context)
{
    SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(context->Device);
    if (cmdbuf == NULL)
    {
        SDL_Log("AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return -1;
    }

	SDL_SubmitGPUCommandBuffer(cmdbuf);

	return 0;
}

static void Quit(Context* context)
{
	CommonQuit(context);
}

Example BasicVr_Example = { "BasicVr", Init, Update, Draw, Quit };
