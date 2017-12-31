//------------------------------------------------------------------------------
// <copyright file="BackgroundRemovalBasics.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

#include "stdafx.h"
#include <vector>
#include "BackgroundRemovalBasics.h"
#include "resource.h"

#include <Wincodec.h>
#include <assert.h>

#include <NuiSensorChooser.h>
#include <NuiSensorChooserUI.h>
#include <NuiApi.h>

#define WM_SENSORCHANGED WM_USER + 1

#ifndef HINST_THISCOMPONENT
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)
#endif

/// <summary>
/// Entry point for the application
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="hPrevInstance">always 0</param>
/// <param name="lpCmdLine">command line arguments</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
/// <returns>status</returns>
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    CBackgroundRemovalBasics application;
    application.Run(hInstance, nCmdShow);
}

/// <summary>
/// Constructor
/// </summary>
CBackgroundRemovalBasics::CBackgroundRemovalBasics() :
    m_hNextDepthFrameEvent(INVALID_HANDLE_VALUE),
    m_hNextColorFrameEvent(INVALID_HANDLE_VALUE),
    m_hNextSkeletonFrameEvent(INVALID_HANDLE_VALUE),
    m_hNextBackgroundRemovedFrameEvent(INVALID_HANDLE_VALUE),
    m_pDepthStreamHandle(INVALID_HANDLE_VALUE),
    m_pColorStreamHandle(INVALID_HANDLE_VALUE),
    m_bNearMode(false),
    m_pNuiSensor(NULL),
    m_pSensorChooser(NULL),
    m_pSensorChooserUI(NULL),
    m_pBackgroundRemovalStream(NULL),
    m_trackedSkeleton(NUI_SKELETON_INVALID_TRACKING_ID)
{
    DWORD width = 0;
    DWORD height = 0;

    NuiImageResolutionToSize(cDepthResolution, width, height);
    m_depthWidth  = static_cast<LONG>(width);
    m_depthHeight = static_cast<LONG>(height);

    NuiImageResolutionToSize(cColorResolution, width, height);
    m_colorWidth  = static_cast<LONG>(width);
    m_colorHeight = static_cast<LONG>(height);;

    // create heap storage for depth pixel data in RGBX format
    m_outputRGBX = new BYTE[m_colorWidth * m_colorHeight * cBytesPerPixel];
    m_backgroundRGBX = new BYTE[m_colorWidth * m_colorHeight * cBytesPerPixel];

    // Create an event that will be signaled when depth data is available
    m_hNextDepthFrameEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    // Create an event that will be signaled when color data is available
    m_hNextColorFrameEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    // Create an event that will be signaled when skeleton frame is available
    m_hNextSkeletonFrameEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    // Create an event that will be signaled when the segmentation frame is ready
    m_hNextBackgroundRemovedFrameEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

}

/// <summary>
/// Destructor
/// </summary>
CBackgroundRemovalBasics::~CBackgroundRemovalBasics()
{
    // clean up arrays
    delete[] m_outputRGBX;
    delete[] m_backgroundRGBX;

    // clean up Direct2D renderer
    delete m_pDrawBackgroundRemovalBasics;
    m_pDrawBackgroundRemovalBasics = NULL;

    // clean up NSC sensor chooser and its UI control
    delete m_pSensorChooser;
    delete m_pSensorChooserUI;

    SafeCloseHandle(m_hNextBackgroundRemovedFrameEvent);
    SafeCloseHandle(m_hNextColorFrameEvent);
    SafeCloseHandle(m_hNextDepthFrameEvent);
    SafeCloseHandle(m_hNextSkeletonFrameEvent);

    SafeRelease(m_pD2DFactory);
    SafeRelease(m_pNuiSensor);
    SafeRelease(m_pBackgroundRemovalStream);
}

/// <summary>
/// Creates the main window and begins processing
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
int CBackgroundRemovalBasics::Run(HINSTANCE hInstance, int nCmdShow)
{
    MSG       msg = {0};
    WNDCLASS  wc;

    // Dialog custom window class
    ZeroMemory(&wc, sizeof(wc));
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.cbWndExtra    = DLGWINDOWEXTRA;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP));
    wc.lpfnWndProc   = DefDlgProcW;
    wc.lpszClassName = L"BackgroundRemovalBasicsAppDlgWndClass";

    if (!RegisterClassW(&wc))
    {
        return 0;
    }

    // Create main application window
    HWND hWndApp = CreateDialogParamW(
        hInstance,
        MAKEINTRESOURCE(IDD_APP),
        NULL,
        (DLGPROC)CBackgroundRemovalBasics::MessageRouter, 
        reinterpret_cast<LPARAM>(this));

    // Set the init sensor status
    UpdateNscControlStatus();

    // Show window
    ShowWindow(hWndApp, nCmdShow);
    UpdateWindow(hWndApp);

   
    const HANDLE hEvents[] = {m_hNextDepthFrameEvent, m_hNextColorFrameEvent, m_hNextSkeletonFrameEvent, m_hNextBackgroundRemovedFrameEvent};
    LoadResourceImage(L"Background", L"Image", m_colorWidth * m_colorHeight * cBytesPerPixel, m_backgroundRGBX);

    // Main message loop
    while (WM_QUIT != msg.message)
    {
        // Check to see if we have either a message (by passing in QS_ALLINPUT)
        // Or a Kinect event (hEvents)
        // Update() will check for Kinect events individually, in case more than one are signaled
        MsgWaitForMultipleObjects(_countof(hEvents), hEvents, FALSE, INFINITE, QS_ALLINPUT);

        // Individually check the Kinect stream events since MsgWaitForMultipleObjects
        // can return for other reasons even though these are signaled.
        Update();
        
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            // If a dialog message will be taken care of by the dialog proc
            if ((hWndApp != NULL) && IsDialogMessageW(hWndApp, &msg))
            {
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return static_cast<int>(msg.wParam);
}

/// <summary>
/// This function will be called when Kinect device status changed
/// </summary>
void CALLBACK CBackgroundRemovalBasics::StatusChangeCallback(HRESULT hrStatus, const OLECHAR* instancename, const OLECHAR* uniqueDeviceName, void* pUserData)
{
    HWND hWnd = reinterpret_cast<HWND>(pUserData);

    if (NULL != hWnd)
    {
        SendMessage(hWnd, WM_SENSORCHANGED, 0, 0);
    }
}

/// <summary>
/// Main processing function
/// </summary>
void CBackgroundRemovalBasics::Update()
{
    if (NULL == m_pNuiSensor)
    {
        return;
    }

    if (WAIT_OBJECT_0 == WaitForSingleObject(m_hNextBackgroundRemovedFrameEvent, 0))
    {
        ComposeImage();
    }

    if ( WAIT_OBJECT_0 == WaitForSingleObject(m_hNextDepthFrameEvent, 0) )
    {
        ProcessDepth();
    }

    if ( WAIT_OBJECT_0 == WaitForSingleObject(m_hNextColorFrameEvent, 0) )
    {
        ProcessColor();
    }

    if (WAIT_OBJECT_0 == WaitForSingleObject(m_hNextSkeletonFrameEvent, 0) )
    {
        ProcessSkeleton();
    }
}

/// <summary>
/// Handles window messages, passes most to the class instance to handle
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CBackgroundRemovalBasics::MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CBackgroundRemovalBasics* pThis = NULL;
    
    if (WM_INITDIALOG == uMsg)
    {
        pThis = reinterpret_cast<CBackgroundRemovalBasics*>(lParam);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else
    {
        pThis = reinterpret_cast<CBackgroundRemovalBasics*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pThis)
    {
        return pThis->DlgProc(hWnd, uMsg, wParam, lParam);
    }

    return 0;
}

/// <summary>
/// Handle windows messages for the class instance
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CBackgroundRemovalBasics::DlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            HRESULT hr;
            // Bind application window handle
            m_hWnd = hWnd;

            // Create NuiSensorChooser UI control
            RECT rc;
            GetClientRect(m_hWnd, &rc);

            POINT ptCenterTop;
            ptCenterTop.x = (rc.right - rc.left)/2;
            ptCenterTop.y = 0;

            // Create the sensor chooser UI control to show sensor status
            m_pSensorChooserUI = new NuiSensorChooserUI(m_hWnd, IDC_SENSORCHOOSER, ptCenterTop);

            // Set the sensor status callback
            NuiSetDeviceStatusCallback(StatusChangeCallback, reinterpret_cast<void*>(m_hWnd));
            // Init the sensor chooser to find a valid sensor
            m_pSensorChooser = new NuiSensorChooser();

            // Init Direct2D
            D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);

            // Create and initialize a new Direct2D image renderer (take a look at ImageRenderer.h)
            // We'll use this to draw the data we receive from the Kinect to the screen
            m_pDrawBackgroundRemovalBasics = new ImageRenderer();

            hr = m_pDrawBackgroundRemovalBasics->Initialize(GetDlgItem(m_hWnd, IDC_VIDEOVIEW),
                m_pD2DFactory, m_colorWidth, m_colorHeight, m_colorWidth * cBytesPerPixel);
            if (FAILED(hr))
            {
                SetStatusMessage(L"Failed to initialize the Direct2D draw device.");
            }

            // Look for a connected Kinect, and create it if found
            hr = CreateFirstConnected();
            if (FAILED(hr))
            {
                return FALSE;
            }

            // Create the sream that does background removal and player segmentation
            hr = CreateBackgroundRemovedColorStream();
            if (FAILED(hr))
            {
                return FALSE;
            }
        }
        break;

        // If the titlebar X is clicked, destroy app
        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            // Quit the main message pump
            PostQuitMessage(0);
            break;

        // Handle button press
        case WM_COMMAND:
            // If it was for the near mode control and a clicked event, change near mode
            if (IDC_CHECK_NEARMODE == LOWORD(wParam) && BN_CLICKED == HIWORD(wParam))
            {
                // Toggle out internal state for near mode
                m_bNearMode = !m_bNearMode;

                if (NULL != m_pNuiSensor)
                {
                    // Set near mode based on our internal state
                    m_pNuiSensor->NuiImageStreamSetImageFrameFlags(m_pDepthStreamHandle, m_bNearMode ? NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE : 0);
                }
            }
            break;

        case WM_NOTIFY:
        {
            const NMHDR* pNMHeader = reinterpret_cast<const NMHDR*>(lParam);
            if (pNMHeader->code == NSCN_REFRESH && pNMHeader->idFrom == IDC_SENSORCHOOSER)
            {
                // Handle refresh notification sent from NSC UI control
                DWORD dwChangeFlags;

                HRESULT hr = m_pSensorChooser->TryResolveConflict(&dwChangeFlags);
                if (SUCCEEDED(hr))
                {
                    UpdateSensorAndStatus(dwChangeFlags);
                }
            }

            return TRUE;
        }
        break;

        case WM_SENSORCHANGED:
        {
            if (NULL != m_pSensorChooser)
            {
                // Handle sensor status change event
                DWORD dwChangeFlags = 0;

                HRESULT hr = m_pSensorChooser->HandleNuiStatusChanged(&dwChangeFlags);
                if (SUCCEEDED(hr))
                {
                    UpdateSensorAndStatus(dwChangeFlags);
                }
            }
        }
        break;
    }

    return FALSE;
}

/// <summary>
/// Create the first connected Kinect found
/// </summary>
/// <returns>S_OK on success, otherwise failure code</returns>
HRESULT CBackgroundRemovalBasics::CreateFirstConnected()
{
    // Get the Kinect and specify that we'll be using depth
    HRESULT hr = m_pSensorChooser->GetSensor(NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX | NUI_INITIALIZE_FLAG_USES_COLOR, &m_pNuiSensor);

    if (SUCCEEDED(hr) && NULL != m_pNuiSensor)
    {
        // Open a depth image stream to receive depth frames
        hr = m_pNuiSensor->NuiImageStreamOpen(
            NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX,
            cDepthResolution,
            0,
            2,
            m_hNextDepthFrameEvent,
            &m_pDepthStreamHandle);
        if (SUCCEEDED(hr))
        {
            // Open a color image stream to receive color frames
            hr = m_pNuiSensor->NuiImageStreamOpen(
                NUI_IMAGE_TYPE_COLOR,
                cColorResolution,
                0,
                2,
                m_hNextColorFrameEvent,
                &m_pColorStreamHandle);

            if (SUCCEEDED(hr))
            {
                hr = m_pNuiSensor->NuiSkeletonTrackingEnable(m_hNextSkeletonFrameEvent, NUI_SKELETON_TRACKING_FLAG_ENABLE_IN_NEAR_RANGE);
            }
        }
    }

    if (NULL == m_pNuiSensor || FAILED(hr))
    {
        SafeRelease(m_pNuiSensor);
        // Reset all the event to nonsignaled state
        ResetEvent(m_hNextDepthFrameEvent);
        ResetEvent(m_hNextColorFrameEvent);
        ResetEvent(m_hNextSkeletonFrameEvent);
        SetStatusMessage(L"No ready Kinect found!");
        return E_FAIL;
    }
    else
    {
        SetStatusMessage(L"Kinect found!");
    }

    return hr;
}

/// <summary>
/// Create the stream that removes the background and display a single player specified
/// </summary>
/// <returns>S_OK on success, otherwise failure code</returns>
HRESULT CBackgroundRemovalBasics::CreateBackgroundRemovedColorStream()
{
    HRESULT hr;

    if (NULL == m_pNuiSensor)
    {
        return E_FAIL;
    }

    hr = NuiCreateBackgroundRemovedColorStream(m_pNuiSensor, &m_pBackgroundRemovalStream);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = m_pBackgroundRemovalStream->Enable(cColorResolution, cDepthResolution, m_hNextBackgroundRemovedFrameEvent);

    return hr;
}

/// <summary>
/// Handle new depth data
/// </summary>
/// <returns>S_OK on success, otherwise failure code</returns>
HRESULT CBackgroundRemovalBasics::ProcessDepth()
{
    HRESULT hr;
	HRESULT bghr = S_OK;
    NUI_IMAGE_FRAME imageFrame;

    // Attempt to get the depth frame
    LARGE_INTEGER depthTimeStamp;
    hr = m_pNuiSensor->NuiImageStreamGetNextFrame(m_pDepthStreamHandle, 0, &imageFrame);
    if (FAILED(hr))
    {
        return hr;
    }
    depthTimeStamp = imageFrame.liTimeStamp;
    INuiFrameTexture* pTexture;

    // Attempt to get the extended depth texture
    hr = m_pNuiSensor->NuiImageFrameGetDepthImagePixelFrameTexture(m_pDepthStreamHandle, &imageFrame, &m_bNearMode, &pTexture);
    if (FAILED(hr))
    {
        return hr;
    }
    NUI_LOCKED_RECT LockedRect;

    // Lock the frame data so the Kinect knows not to modify it while we're reading it
    pTexture->LockRect(0, &LockedRect, NULL, 0);

    // Make sure we've received valid data, and then present it to the background removed color stream. 
	if (LockedRect.Pitch != 0)
	{
		bghr = m_pBackgroundRemovalStream->ProcessDepth(m_depthWidth * m_depthHeight * cBytesPerPixel, LockedRect.pBits, depthTimeStamp);
	}

    // We're done with the texture so unlock it. Even if above process failed, we still need to unlock and release.
    pTexture->UnlockRect(0);
    pTexture->Release();

    // Release the frame
    hr = m_pNuiSensor->NuiImageStreamReleaseFrame(m_pDepthStreamHandle, &imageFrame);

	if (FAILED(bghr))
	{
		return bghr;
	}

    return hr;
}

/// <summary>
/// Handle new color data
/// </summary>
/// <returns>S_OK for success or error code</returns>
HRESULT CBackgroundRemovalBasics::ProcessColor()
{
    HRESULT hr;
	HRESULT bghr = S_OK;

    NUI_IMAGE_FRAME imageFrame;

    // Attempt to get the depth frame
    LARGE_INTEGER colorTimeStamp;
    hr = m_pNuiSensor->NuiImageStreamGetNextFrame(m_pColorStreamHandle, 0, &imageFrame);
    if (FAILED(hr))
    {
        return hr;
    }
    colorTimeStamp = imageFrame.liTimeStamp;

    INuiFrameTexture * pTexture = imageFrame.pFrameTexture;
    NUI_LOCKED_RECT LockedRect;

    // Lock the frame data so the Kinect knows not to modify it while we're reading it
    pTexture->LockRect(0, &LockedRect, NULL, 0);

	// Make sure we've received valid data. Then save a copy of color frame.
	if (LockedRect.Pitch != 0)
	{
		bghr = m_pBackgroundRemovalStream->ProcessColor(m_colorWidth * m_colorHeight * cBytesPerPixel, LockedRect.pBits, colorTimeStamp);
    }

    // We're done with the texture so unlock it
    pTexture->UnlockRect(0);

    // Release the frame
    hr = m_pNuiSensor->NuiImageStreamReleaseFrame(m_pColorStreamHandle, &imageFrame);

	if (FAILED(bghr))
	{
		return bghr;
	}

    return hr;
}

/// <summary>
/// Handle new skeleton data
/// </summary>
/// <returns>S_OK for success or error code</returns>
HRESULT CBackgroundRemovalBasics::ProcessSkeleton()
{
    HRESULT hr;

	NUI_SKELETON_FRAME skeletonFrame;
    hr = m_pNuiSensor->NuiSkeletonGetNextFrame(0, &skeletonFrame);
    if (FAILED(hr))
    {
        return hr;
    }

	NUI_SKELETON_DATA* pSkeletonData = skeletonFrame.SkeletonData;
    // Background Removal Stream requires us to specifically tell it what skeleton ID to use as the foreground
	hr = ChooseSkeleton(pSkeletonData);
	if (FAILED(hr))
    {
        return hr;
    }

    hr = m_pBackgroundRemovalStream->ProcessSkeleton(NUI_SKELETON_COUNT, pSkeletonData, skeletonFrame.liTimeStamp);

    return hr;
}

/// <summary>
/// compose the background removed color image with the background image
/// </summary>
/// <returns>S_OK on success, otherwise failure code</returns>
HRESULT  CBackgroundRemovalBasics::ComposeImage()
{
    HRESULT hr;
    NUI_BACKGROUND_REMOVED_COLOR_FRAME bgRemovedFrame;

    hr = m_pBackgroundRemovalStream->GetNextFrame(0, &bgRemovedFrame);
    if (FAILED(hr))
    {
        return hr;
    }

    const BYTE* pBackgroundRemovedColor = bgRemovedFrame.pBackgroundRemovedColorData;

    int dataLength = static_cast<int>(m_colorWidth) * static_cast<int>(m_colorHeight) * cBytesPerPixel;
    BYTE alpha = 0;
    const int alphaChannelBytePosition = 3;
    for (int i = 0; i < dataLength; ++i)
    {
        if (i % cBytesPerPixel == 0)
        {
            alpha = pBackgroundRemovedColor[i + alphaChannelBytePosition];
        }

        if (i % cBytesPerPixel != alphaChannelBytePosition)
        {
            m_outputRGBX[i] = static_cast<BYTE>(
                ( (UCHAR_MAX - alpha) * m_backgroundRGBX[i] + alpha * pBackgroundRemovedColor[i] ) / UCHAR_MAX
                );
        }
    }

    hr = m_pBackgroundRemovalStream->ReleaseFrame(&bgRemovedFrame);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = m_pDrawBackgroundRemovalBasics->Draw(m_outputRGBX, m_colorWidth * m_colorHeight * cBytesPerPixel);

    return hr;
}

/// <summary>
/// Use the sticky player logic to determine the player whom the background removed
/// color stream should consider as foreground.
/// </summary>
/// <returns>S_OK on success, otherwise failure code</returns>
HRESULT CBackgroundRemovalBasics::ChooseSkeleton(NUI_SKELETON_DATA* pSkeletonData)
{
	HRESULT hr = S_OK;

	// First we go through the stream to find the closest skeleton, and also check whether our current tracked
	// skeleton is still visibile in the stream
	float closestSkeletonDistance = FLT_MAX;
	DWORD closestSkeleton = NUI_SKELETON_INVALID_TRACKING_ID;
	BOOL isTrackedSkeletonVisible = false;
	for (int i = 0; i < NUI_SKELETON_COUNT; ++i)
	{
		NUI_SKELETON_DATA skeleton = pSkeletonData[i];
		if (NUI_SKELETON_TRACKED == skeleton.eTrackingState)
		{
			if (m_trackedSkeleton == skeleton.dwTrackingID)
			{
				isTrackedSkeletonVisible = true;
				break;
			}

			if (skeleton.Position.z < closestSkeletonDistance)
			{
				closestSkeleton = skeleton.dwTrackingID;
				closestSkeletonDistance = skeleton.Position.z;
			}
		}
	}

	// Now we choose a new skeleton unless the currently tracked skeleton is still visible
	if (!isTrackedSkeletonVisible && closestSkeleton != NUI_SKELETON_INVALID_TRACKING_ID)
	{
		hr = m_pBackgroundRemovalStream->SetTrackedPlayer(closestSkeleton);
		if (FAILED(hr))
		{
			return hr;
		}

		m_trackedSkeleton = closestSkeleton;
	}

	return hr;
}

/// <summary>
/// Load an image from a resource into a buffer
/// </summary>
/// <param name="resourceName">name of image resource to load</param>
/// <param name="resourceType">type of resource to load</param>
/// <param name="cOutputBuffer">size of output buffer, in bytes</param>
/// <param name="outputBuffer">buffer that will hold the loaded image</param>
/// <returns>S_OK on success, otherwise failure code</returns>
HRESULT CBackgroundRemovalBasics::LoadResourceImage(
    PCWSTR resourceName,
    PCWSTR resourceType,
    DWORD cOutputBuffer,
    BYTE* outputBuffer
    )
{
    HRESULT hr = S_OK;

    IWICImagingFactory* pIWICFactory = NULL;
    IWICBitmapDecoder* pDecoder = NULL;
    IWICBitmapFrameDecode* pSource = NULL;
    IWICStream* pStream = NULL;
    IWICFormatConverter* pConverter = NULL;
    IWICBitmapScaler* pScaler = NULL;

    HRSRC imageResHandle = NULL;
    HGLOBAL imageResDataHandle = NULL;
    void *pImageFile = NULL;
    DWORD imageFileSize = 0;

    hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (LPVOID*)&pIWICFactory);
    if ( FAILED(hr) ) return hr;

    // Locate the resource.
    imageResHandle = FindResourceW(HINST_THISCOMPONENT, resourceName, resourceType);
    hr = imageResHandle ? S_OK : E_FAIL;

    if (SUCCEEDED(hr))
    {
        // Load the resource.
        imageResDataHandle = LoadResource(HINST_THISCOMPONENT, imageResHandle);
        hr = imageResDataHandle ? S_OK : E_FAIL;
    }

    if (SUCCEEDED(hr))
    {
        // Lock it to get a system memory pointer.
        pImageFile = LockResource(imageResDataHandle);
        hr = pImageFile ? S_OK : E_FAIL;
    }

    if (SUCCEEDED(hr))
    {
        // Calculate the size.
        imageFileSize = SizeofResource(HINST_THISCOMPONENT, imageResHandle);
        hr = imageFileSize ? S_OK : E_FAIL;
    }

    if (SUCCEEDED(hr))
    {
        // Create a WIC stream to map onto the memory.
        hr = pIWICFactory->CreateStream(&pStream);
    }

    if (SUCCEEDED(hr))
    {
        // Initialize the stream with the memory pointer and size.
        hr = pStream->InitializeFromMemory(
            reinterpret_cast<BYTE*>(pImageFile),
            imageFileSize
            );
    }

    if (SUCCEEDED(hr))
    {
        // Create a decoder for the stream.
        hr = pIWICFactory->CreateDecoderFromStream(
            pStream,
            NULL,
            WICDecodeMetadataCacheOnLoad,
            &pDecoder
            );
    }

    if (SUCCEEDED(hr))
    {
        // Create the initial frame.
        hr = pDecoder->GetFrame(0, &pSource);
    }

    if (SUCCEEDED(hr))
    {
        // Convert the image format to 32bppPBGRA
        // (DXGI_FORMAT_B8G8R8A8_UNORM + D2D1_ALPHA_MODE_PREMULTIPLIED).
        hr = pIWICFactory->CreateFormatConverter(&pConverter);
    }

    if (SUCCEEDED(hr))
    {
        hr = pIWICFactory->CreateBitmapScaler(&pScaler);
    }

    if (SUCCEEDED(hr))
    {
        hr = pScaler->Initialize(
            pSource,
            m_colorWidth,
            m_colorHeight,
            WICBitmapInterpolationModeCubic
            );
    }

    if (SUCCEEDED(hr))
    {
        hr = pConverter->Initialize(
            pScaler,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            NULL,
            0.f,
            WICBitmapPaletteTypeMedianCut
            );
    }

    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(hr))
    {
        hr = pConverter->GetSize(&width, &height);
    }

    // make sure the output buffer is large enough
    if (SUCCEEDED(hr))
    {
        if ( width*height*cBytesPerPixel > cOutputBuffer )
        {
            hr = E_FAIL;
        }
    }

    if (SUCCEEDED(hr))
    {
        hr = pConverter->CopyPixels(NULL, width*cBytesPerPixel, cOutputBuffer, outputBuffer);
    }

    SafeRelease(pScaler);
    SafeRelease(pConverter);
    SafeRelease(pSource);
    SafeRelease(pDecoder);
    SafeRelease(pStream);
    SafeRelease(pIWICFactory);

    return hr;
}

/// <summary>
/// Set the status bar message
/// </summary>
/// <param name="szMessage">message to display</param>
void CBackgroundRemovalBasics::SetStatusMessage(WCHAR * szMessage)
{
    SendDlgItemMessageW(m_hWnd, IDC_STATUS, WM_SETTEXT, 0, (LPARAM)szMessage);
}

/// <summary>
/// Update the sensor and status based on the input changeg flags
/// <param name="changedFlags">Sensor chooser flag indicating kinect sensor change status</param>
/// </summary>
void CBackgroundRemovalBasics::UpdateSensorAndStatus(DWORD changedFlags)
{
    switch(changedFlags)
    {
    case NUISENSORCHOOSER_SENSOR_CHANGED_FLAG:
        {
            // Free the previous sensor and try to get a new one
            SafeRelease(m_pNuiSensor);
            HRESULT hr = CreateFirstConnected();
            if (SUCCEEDED(hr))
            {
                CreateBackgroundRemovedColorStream();
            }
        }

    case NUISENSORCHOOSER_STATUS_CHANGED_FLAG:
        UpdateNscControlStatus();
        break;
    }
}

/// <summary>
/// Update the Nui Sensor Chooser UI control status
/// </summary>
void CBackgroundRemovalBasics::UpdateNscControlStatus()
{
    assert(m_pSensorChooser != NULL);
    assert(m_pSensorChooserUI != NULL);

    DWORD dwStatus;
    HRESULT hr = m_pSensorChooser->GetStatus(&dwStatus);

    if (SUCCEEDED(hr))
    {
        m_pSensorChooserUI->UpdateSensorStatus(dwStatus);
    }
}

