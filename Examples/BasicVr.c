#include "Common.h"

#include "openxr/openxr.h"

static XrInstance instance = NULL;
static XrSystemId systemId = 0;
static XrSession session = NULL;
static bool doXrFrameLoop = false;
static Uint32 num_view_configurations = 0;
static XrSwapchain swapchain = NULL;
static SDL_GPUTexture **swapchain_textures = NULL;
static XrSpace localSpace = NULL;
static XrExtent2Di swapchainSize = {0, 0};

#define XR_ERR_RET(resultExpression, retval)                                \
    do {                                                                    \
		XrResult resolvedResult = (resultExpression); 						\
		if (XR_FAILED(resolvedResult)) {                   					\
			char resultString[XR_MAX_RESULT_STRING_SIZE]; 					\
			SDL_memset(resultString, 0, SDL_arraysize(resultString)); 		\
			(void)xrResultToString(instance, resolvedResult, resultString); \
			SDL_SetError("Got OpenXR error %s", resultString);				\
			return (retval);                      							\
		} 																	\
	} while (0)

static int Init(Context* context)
{
	XrResult result;

	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, true);

	if (!SDL_CreateXRGPUDeviceWithProperties(&context->Device, &instance, &systemId, props))
	{
		SDL_Log("SDL_CreateXRGPUDeviceWithProperties failed");
		return -1;
	}

	XrSessionCreateInfo sessionCreateInfo = {XR_TYPE_SESSION_CREATE_INFO};
	XR_ERR_RET(SDL_CreateGPUXRSession(context->Device, &sessionCreateInfo, &session), -1);

	return 0;
}

static int HandleStateChangedEvent(Context* context, XrEventDataSessionStateChanged* event)
{
	switch(event->state)
	{
		case XR_SESSION_STATE_READY:
		{
			XrSessionBeginInfo sessionBeginInfo = {XR_TYPE_SESSION_BEGIN_INFO};
			sessionBeginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
			XR_ERR_RET(xrBeginSession(session, &sessionBeginInfo), -1);

			SDL_Log("Begun OpenXR session");
			
			XR_ERR_RET(xrEnumerateViewConfigurationViews(
				instance, 
				systemId, 
				XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 
				0, 
				&num_view_configurations, 
				NULL), -1);

			XrViewConfigurationView *view_configuration_views = SDL_stack_alloc(XrViewConfigurationView, num_view_configurations);
			for (Uint32 i = 0; i < num_view_configurations; i++)
				view_configuration_views[i] = (XrViewConfigurationView){XR_TYPE_VIEW_CONFIGURATION_VIEW};
			
			XR_ERR_RET(xrEnumerateViewConfigurationViews(
				instance, 
				systemId, 
				XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 
				num_view_configurations, 
				&num_view_configurations, 
				view_configuration_views), -1);

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

				swapchainSize = (XrExtent2Di){view_configuration_views->recommendedImageRectWidth, view_configuration_views->recommendedImageRectHeight};
			}

			SDL_stack_free(view_configuration_views);

			XrSwapchainCreateInfo swapchainCreateInfo = {
				.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
				.width = swapchainSize.width,
				.height = swapchainSize.height,
				.mipCount = 1,
				.sampleCount = 1,
				.faceCount = 1,
				.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT, /* sure..? not sure if anything else is needed. */
				.arraySize = 1};

			SDL_GPUTextureFormat swapchainFormat;
			XR_ERR_RET(SDL_CreateGPUXRSwapchain(
				context->Device, 
				session, 
				&swapchainCreateInfo, 
				&swapchainFormat, 
				&swapchain, 
				&swapchain_textures), -1);

			doXrFrameLoop = true;

			XR_ERR_RET(xrCreateReferenceSpace(session, &(XrReferenceSpaceCreateInfo){
				.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
				.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL,
				.poseInReferenceSpace = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
			}, &localSpace), -1);

			break;
		}
		case XR_SESSION_STATE_STOPPING:
		{
			doXrFrameLoop = false;

			XR_ERR_RET(xrEndSession(session), -1);

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

	return 0;
}

static int HandleXrEvent(Context* context) 
{
	XrResult result;
	XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
	XR_ERR_RET(result = xrPollEvent(instance, &event), -1);
	if(result == XR_SUCCESS) {
		switch(event.type)
		{
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: 
			{
				XrEventDataSessionStateChanged *stateChangedEvent = ((XrEventDataSessionStateChanged *)&event);

				if(HandleStateChangedEvent(context, stateChangedEvent) != 0) return -1;

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

static int Update(Context* context)
{
	static int frameCount = 0;
	XrResult result;

	SDL_Log("frame count: %d", frameCount++);
	if(HandleXrEvent(context) != 0) return -1;

	return 0;
}

static int Draw(Context* context)
{
	// XrResult result;

	if(doXrFrameLoop) 
	{
		XrFrameWaitInfo frameWaitInfo = {XR_TYPE_FRAME_WAIT_INFO};
		XrFrameState frameState = {XR_TYPE_FRAME_STATE};
		// Wait for the next frame
		XR_ERR_RET(xrWaitFrame(session, &frameWaitInfo, &frameState), -1);

		// Begin a new frame
		XrFrameBeginInfo frameBeginInfo = {XR_TYPE_FRAME_BEGIN_INFO};
		XR_ERR_RET(xrBeginFrame(session, &frameBeginInfo), -1);

		// If we need to render, fill out the projection views for each eye
		XrCompositionLayerProjectionView projectionViews[2];
		if(frameState.shouldRender)
		{
			// Get a GPU cmd buffer
			SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(context->Device);
			if (cmdbuf == NULL)
			{
				SDL_Log("AcquireGPUCommandBuffer failed: %s", SDL_GetError());
				return -1;
			}

			Uint32 viewCount;
			XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
			XrViewState viewState = {XR_TYPE_VIEW_STATE};
			// TODO: handle viewCount not being 2
			XR_ERR_RET(xrLocateViews(session, &(XrViewLocateInfo){
				.type = XR_TYPE_VIEW_LOCATE_INFO,
				.displayTime = frameState.predictedDisplayTime,
				.space = localSpace,
				.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
			}, &viewState, 2, &viewCount, views), -1);

			Uint32 swapchainIndex;
			XR_ERR_RET(xrAcquireSwapchainImage(swapchain, NULL, &swapchainIndex), -1);

			XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
			waitInfo.timeout = XR_INFINITE_DURATION; /* spec says the runtime *must* never block indefinitely, so this is always safe! but maybe we *should* tune this? not sure */
			XR_ERR_RET(xrWaitSwapchainImage(swapchain, &waitInfo), -1);

			/* we got the texture we're going to render with! */
			SDL_GPUTexture *swapchain_texture = swapchain_textures[swapchainIndex];

			SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(cmdbuf, &(SDL_GPUColorTargetInfo){
				.texture = swapchain_texture,
				.clear_color = {0.5, 1, 0.5, 1},
				.load_op = SDL_GPU_LOADOP_CLEAR,
				.store_op = SDL_GPU_STOREOP_STORE,
			}, 1, NULL);

			SDL_EndGPURenderPass(render_pass);

			SDL_SubmitGPUCommandBuffer(cmdbuf);

			XR_ERR_RET(xrReleaseSwapchainImage(swapchain, NULL), -1);

			for(Uint32 i = 0; i < 2; i++) {
				projectionViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
				projectionViews[i].fov = views[i].fov;
				projectionViews[i].pose = views[i].pose;
				projectionViews[i].subImage = (XrSwapchainSubImage){
					.swapchain = swapchain,
					.imageArrayIndex = 0,
					.imageRect = {.offset = {0}, .extent = swapchainSize},
				};
			}
		}

		XrFrameEndInfo frameEndInfo = {XR_TYPE_FRAME_END_INFO};
		frameEndInfo.displayTime = frameState.predictedDisplayTime;
		frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

		frameEndInfo.layerCount = frameState.shouldRender ? 1 : 0;

		const XrCompositionLayerBaseHeader *projectionLayers[1];
		projectionLayers[0] = (const XrCompositionLayerBaseHeader*) &(XrCompositionLayerProjection){
			.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
			.space = localSpace,
			.viewCount = 2,
			.views = projectionViews,
		};
		frameEndInfo.layers = projectionLayers;

		XR_ERR_RET(xrEndFrame(session, &frameEndInfo), -1);
	}

	return 0;
}

static void Quit(Context* context)
{
	if (localSpace) xrDestroySpace(localSpace);
	if (swapchain) SDL_DestroyGPUXRSwapchain(context->Device, swapchain, swapchain_textures);
	if (session) xrDestroySession(session);
	if (instance) xrDestroyInstance(instance);

	CommonQuit(context);
}

Example BasicVr_Example = { "BasicVr", Init, Update, Draw, Quit };
